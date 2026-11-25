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
        $cmds = $db->query("SELECT id, dev, act, pos FROM cmds WHERE state='new' ORDER BY id")->fetchAll();
        $view = (float)kv_get('last_view', 0);
        $active = (microtime(true) - $view) < ACTIVE_SECS;
        if ($cmds || microtime(true) >= $end || ($wait && $view > $view0 && $active)) break;
        usleep(500000);
    }
    if ($cmds) $db->exec("UPDATE cmds SET state='sent' WHERE state='new' AND id<=" . (int)end($cmds)['id']);
    out(['ok' => true, 'commands' => $cmds, 'active' => $active,
         'need_full' => kv_get('devices') === null]);
}

// ---------------- Browser ----------------
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
         'devices' => kv_get('devices', []), 'alerts' => $alerts, 'cmds' => $cmds->fetchAll(),
         'read_only' => READ_ONLY]);
}

if ($a === 'cmd' && $post) {
    if (READ_ONLY) out(['error' => 'Portal ist schreibgeschützt'], 403);
    $in = json_decode(file_get_contents('php://input'), true) ?: [];
    $dev = (string)($in['dev'] ?? '');
    $act = (string)($in['act'] ?? '');
    if (!in_array($act, ['on', 'off', 'toggle', 'open', 'close', 'stop', 'pos'], true)) out(['error' => 'Unbekannte Aktion'], 400);
    $known = array_column(kv_get('devices', []), 'id');
    if (!in_array($dev, $known, true)) out(['error' => 'Unbekanntes Gerät'], 400);
    $pos = $act === 'pos' ? max(0, min(100, (int)($in['pos'] ?? 0))) : null;
    db()->prepare('INSERT INTO cmds(ts, dev, act, pos) VALUES(?,?,?,?)')->execute([microtime(true), $dev, $act, $pos]);
    kv_set('last_view', microtime(true));
    out(['ok' => true, 'id' => (int)db()->lastInsertId()]);
}

out(['error' => 'Unbekannt'], 404);
