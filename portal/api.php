<?php
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
// Pagurus Portal – API
//   a=sync   (Pagurus Cloud, Authorization: Bearer SYNC_TOKEN): Zustand hochladen, Befehle abholen
//   a=login / a=logout / a=state / a=cmd   (Browser, Anmeldung per Sitzung)
require __DIR__ . '/lib.php';

$a = $_GET['a'] ?? '';

// ---------------- Abgleich mit der Pagurus Cloud ----------------
if ($a === 'sync') {
    $auth = $_SERVER['HTTP_AUTHORIZATION'] ?? $_SERVER['REDIRECT_HTTP_AUTHORIZATION'] ?? ($_SERVER['HTTP_X_PAGURUS_TOKEN'] ?? '');
    $tok = preg_replace('/^Bearer\s+/i', '', $auth);
    if (SYNC_TOKEN === '' || str_starts_with(SYNC_TOKEN, 'HIER-') || !hash_equals(SYNC_TOKEN, $tok)) {
        out(['error' => 'Schlüssel falsch'], 403);
    }
    $in = json_decode(file_get_contents('php://input'), true) ?: [];
    $db = db();
    $db->beginTransaction();
    $now = microtime(true);
    kv_set('last_sync', $now);
    if (isset($in['cloud'])) kv_set('cloud', $in['cloud']);
    if (isset($in['devices']) && is_array($in['devices'])) kv_set('devices', $in['devices']);
    if (isset($in['scenes']) && is_array($in['scenes'])) kv_set('scenes', $in['scenes']);
    if (!empty($in['alerts_reset'])) $db->exec('DELETE FROM alerts');
    if (isset($in['alerts']) && is_array($in['alerts'])) {
        $ins = $db->prepare('INSERT OR REPLACE INTO alerts(id, ts, dev, name, kind, text, ack) VALUES(?,?,?,?,?,?,?)');
        foreach ($in['alerts'] as $x) {
            $ins->execute([(int)$x['id'], (float)$x['ts'], (string)($x['dev'] ?? ''), (string)($x['name'] ?? ''),
                           (string)($x['kind'] ?? ''), (string)($x['text'] ?? ''), (int)($x['ack'] ?? 0)]);
        }
        $db->exec('DELETE FROM alerts WHERE id NOT IN (SELECT id FROM alerts ORDER BY id DESC LIMIT 200)');
    }
    if (isset($in['results']) && is_array($in['results'])) {
        $up = $db->prepare("UPDATE cmds SET state=?, result=?, done=? WHERE id=?");
        foreach ($in['results'] as $r) {
            $up->execute([empty($r['error']) ? 'ok' : 'error', (string)($r['error'] ?? ''), $now, (int)$r['id']]);
        }
    }
    $db->prepare("UPDATE cmds SET state='expired' WHERE state='new' AND ts<?")->execute([$now - CMD_TTL]);
    $db->prepare('DELETE FROM cmds WHERE ts<?')->execute([$now - 86400]);
    $db->commit();

    // Befehle ausliefern. Long-Polling: ist niemand im Portal, wartet die Anfrage bis zu "wait" Sekunden,
    // bis ein Befehl kommt oder jemand das Portal oeffnet – so kommt ein Befehl sofort an.
    $wait = max(0, min(25, (int)($_GET['wait'] ?? 0)));
    set_time_limit($wait + 20);
    $end = $now + $wait;
    $view0 = (float)kv_get('last_view', 0);
    while (true) {
        $cmds = $db->query("SELECT id, dev, act, pos, arg FROM cmds WHERE state='new' ORDER BY id")->fetchAll();
        $relay = $db->query("SELECT id, method, path, body, ctype, role FROM relay WHERE state='new' ORDER BY id LIMIT 20")->fetchAll();
        $view = (float)kv_get('last_view', 0);
        $active = (microtime(true) - $view) < ACTIVE_SECS;
        if ($cmds || $relay || microtime(true) >= $end || ($wait && $view > $view0 && $active)) break;
        usleep(250000);
    }
    if ($cmds) $db->exec("UPDATE cmds SET state='sent' WHERE state='new' AND id<=" . (int)end($cmds)['id']);
    if ($relay) $db->exec("UPDATE relay SET state='sent' WHERE state='new' AND id IN (" . implode(',', array_map('intval', array_column($relay, 'id'))) . ")");
    $db->prepare('DELETE FROM relay WHERE ts<?')->execute([microtime(true) - 300]);
    out(['ok' => true, 'commands' => $cmds, 'relay' => $relay, 'active' => $active,
         'need_full' => kv_get('devices') === null]);
}

// Antwort der Cloud auf eine durchgereichte Anfrage
if ($a === 'relay_result') {
    $auth = $_SERVER['HTTP_AUTHORIZATION'] ?? $_SERVER['REDIRECT_HTTP_AUTHORIZATION'] ?? ($_SERVER['HTTP_X_PAGURUS_TOKEN'] ?? '');
    $tok = preg_replace('/^Bearer\s+/i', '', $auth);
    if (SYNC_TOKEN === '' || str_starts_with(SYNC_TOKEN, 'HIER-') || !hash_equals(SYNC_TOKEN, $tok)) out(['error' => 'Schlüssel falsch'], 403);
    $in = json_decode(file_get_contents('php://input'), true) ?: [];
    db()->prepare("UPDATE relay SET state='done', status=?, rctype=?, rbody=? WHERE id=?")
        ->execute([(int)($in['status'] ?? 502), (string)($in['ctype'] ?? ''), (string)($in['body'] ?? ''), (int)($in['id'] ?? 0)]);
    out(['ok' => true]);
}

// ---------------- Browser / App ----------------
session_start_secure();
$post = $_SERVER['REQUEST_METHOD'] === 'POST';
if ($post && ($_SERVER['HTTP_X_PAGURUS'] ?? '') !== '1') out(['error' => 'Ungültige Anfrage'], 400);   // gegen fremde Formulare

if ($a === 'login' && $post) {
    if (login_blocked()) out(['error' => 'Zu viele Fehlversuche – bitte 15 Minuten warten'], 429);
    $in = json_decode(file_get_contents('php://input'), true) ?: [];
    if (str_starts_with(LOGIN_PASSWORD, 'HIER-') || !hash_equals(LOGIN_PASSWORD, (string)($in['password'] ?? ''))) {
        login_failed();
        out(['error' => 'Passwort falsch'], 403);
    }
    session_regenerate_id(true);
    $_SESSION['ok'] = 1;
    out(['ok' => true]);
}
if ($a === 'logout' && $post) {
    $_SESSION = [];
    session_destroy();
    out(['ok' => true]);
}
if (!logged_in()) out(['error' => 'Nicht angemeldet'], 401);

if ($a === 'state') {
    $now = microtime(true);
    kv_set('last_view', $now);
    session_write_close();
    $alerts = db()->query('SELECT * FROM alerts ORDER BY id DESC LIMIT 100')->fetchAll();
    $cmds = db()->prepare('SELECT id, dev, act, state, result FROM cmds WHERE ts>? ORDER BY id DESC LIMIT 20');
    $cmds->execute([$now - 120]);
    out(['now' => $now, 'last_sync' => kv_get('last_sync', 0), 'cloud' => kv_get('cloud', []),
         'devices' => kv_get('devices', []), 'scenes' => kv_get('scenes', []), 'alerts' => $alerts, 'cmds' => $cmds->fetchAll(),
         'read_only' => READ_ONLY]);
}

// App unterwegs: beliebige Anfrage an die Cloud durchreichen und auf die Antwort warten
if ($a === 'relay' && $post) {
    session_write_close();
    $in = json_decode(file_get_contents('php://input'), true) ?: [];
    $method = strtoupper((string)($in['method'] ?? 'GET'));
    $path = (string)($in['path'] ?? '/');
    if (!in_array($method, ['GET', 'POST', 'PUT', 'DELETE'], true) || $path === '' || $path[0] !== '/') out(['error' => 'Ungültige Anfrage'], 400);
    if (READ_ONLY && $method !== 'GET') out(['error' => 'Portal ist schreibgeschützt'], 403);
    $db = db();
    $db->prepare('INSERT INTO relay(ts, method, path, body, ctype, role) VALUES(?,?,?,?,?,?)')
       ->execute([microtime(true), $method, $path, isset($in['body']) ? (string)$in['body'] : null,
                  (string)($in['ctype'] ?? 'application/json'), READ_ONLY ? 'viewer' : 'admin']);
    $id = (int)$db->lastInsertId();
    kv_set('last_view', microtime(true));
    set_time_limit(60);
    $end = microtime(true) + 30;
    $q = $db->prepare('SELECT state, status, rctype, rbody FROM relay WHERE id=?');
    while (microtime(true) < $end) {
        $q->execute([$id]);
        $r = $q->fetch();
        if ($r && $r['state'] === 'done') {
            $db->prepare('DELETE FROM relay WHERE id=?')->execute([$id]);
            out(['status' => (int)$r['status'], 'ctype' => $r['rctype'], 'body' => $r['rbody']]);
        }
        usleep(150000);
    }
    $db->prepare("DELETE FROM relay WHERE id=?")->execute([$id]);
    out(['error' => 'Cloud antwortet nicht'], 504);
}

// App unterwegs: auf neue Meldungen warten (Push)
if ($a === 'alerts_wait') {
    session_write_close();
    $after = (int)($_GET['after'] ?? 0);
    $wait = max(0, min(50, (int)($_GET['timeout'] ?? 25)));
    set_time_limit($wait + 20);
    $end = microtime(true) + $wait;
    $q = db()->prepare('SELECT * FROM alerts WHERE id>? ORDER BY id LIMIT 50');
    while (true) {
        $q->execute([$after]);
        $rows = $q->fetchAll();
        if ($rows || microtime(true) >= $end) break;
        usleep(1000000);
    }
    $last = (int)db()->query('SELECT MAX(id) FROM alerts')->fetchColumn();
    out(['alerts' => $rows, 'last' => $last, 'last_sync' => kv_get('last_sync', 0)]);
}

if ($a === 'cmd' && $post) {
    if (READ_ONLY) out(['error' => 'Portal ist schreibgeschützt'], 403);
    $in = json_decode(file_get_contents('php://input'), true) ?: [];
    $dev = (string)($in['dev'] ?? '');
    $act = (string)($in['act'] ?? '');
    if (!in_array($act, ['on', 'off', 'toggle', 'open', 'close', 'stop', 'pos', 'bri', 'scene', 'climate'], true)) out(['error' => 'Unbekannte Aktion'], 400);
    $known = $act === 'scene' ? array_map(fn($x) => 'scene:' . $x['id'], kv_get('scenes', []))
                              : array_merge(array_column(kv_get('devices', []), 'id'),
                                            ...array_map(fn($d) => array_column($d['chs'] ?? [], 'id'), kv_get('devices', []) ?: [[]]));
    if (!in_array($dev, $known, true)) out(['error' => 'Unbekanntes Gerät'], 400);
    $pos = ($act === 'pos' || $act === 'bri') ? max(0, min(100, (int)($in['pos'] ?? 0))) : null;
    $arg = null;
    if ($act === 'climate') {                          // nur bekannte Felder mit passenden Werten durchlassen
        $c = is_array($in['arg'] ?? null) ? $in['arg'] : [];
        $p = [];
        if (isset($c['on'])) $p['on'] = (bool)$c['on'];
        if (isset($c['target_C'])) $p['target_C'] = max(16, min(31, round((float)$c['target_C'] * 2) / 2));   // halbe Grad (Midea)
        if (in_array($c['mode'] ?? null, ['cool', 'heat', 'dry', 'fan', 'auto'], true)) $p['mode'] = $c['mode'];
        if (in_array($c['fan'] ?? null, ['auto', 'low', 'low_mid', 'mid', 'mid_high', 'high'], true)) $p['fan'] = $c['fan'];
        foreach (['swing_v', 'swing_h', 'eco', 'turbo', 'mute', 'sleep'] as $k) if (isset($c[$k])) $p[$k] = (bool)$c[$k];
        if (!$p) out(['error' => 'Keine Klima-Werte'], 400);
        $arg = json_encode($p);
    }
    db()->prepare('INSERT INTO cmds(ts, dev, act, pos, arg) VALUES(?,?,?,?,?)')->execute([microtime(true), $dev, $act, $pos, $arg]);
    $id = (int)db()->lastInsertId();
    kv_set('last_view', microtime(true));
    out(['ok' => true, 'id' => $id]);
}

out(['error' => 'Unbekannt'], 404);
