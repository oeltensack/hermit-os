<?php
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
// Pagurus Portal – gemeinsame Funktionen (SQLite im Ordner data/)

if (!is_file(__DIR__ . '/config.php')) {
    http_response_code(500);
    exit('config.php fehlt – config.example.php kopieren und anpassen.');
}
require __DIR__ . '/config.php';

const ACTIVE_SECS = 120;          // so lange nach dem letzten Seitenabruf fragt die Cloud schnell nach
const CMD_TTL = 60;               // Befehle, die die Cloud so lange nicht abholt, verfallen

function db(): PDO {
    static $db = null;
    if ($db) return $db;
    $dir = __DIR__ . '/data';
    if (!is_dir($dir)) mkdir($dir, 0750, true);
    $db = new PDO('sqlite:' . $dir . '/portal.sqlite');
    $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    $db->setAttribute(PDO::ATTR_DEFAULT_FETCH_MODE, PDO::FETCH_ASSOC);
    $db->exec('PRAGMA busy_timeout=4000');
    $db->exec('CREATE TABLE IF NOT EXISTS kv (k TEXT PRIMARY KEY, v TEXT)');
    $db->exec('CREATE TABLE IF NOT EXISTS alerts (id INTEGER PRIMARY KEY, ts REAL, dev TEXT, name TEXT, kind TEXT, text TEXT, ack INTEGER)');
    $db->exec('CREATE TABLE IF NOT EXISTS cmds (id INTEGER PRIMARY KEY AUTOINCREMENT, ts REAL, dev TEXT, act TEXT, pos INTEGER,
               state TEXT DEFAULT \'new\', result TEXT, done REAL)');
    $db->exec('CREATE TABLE IF NOT EXISTS fails (ip TEXT, ts REAL)');
    $db->exec('CREATE TABLE IF NOT EXISTS relay (id INTEGER PRIMARY KEY AUTOINCREMENT, ts REAL, method TEXT, path TEXT,
               body TEXT, ctype TEXT, role TEXT, state TEXT DEFAULT \'new\', status INTEGER, rctype TEXT, rbody TEXT)');
    return $db;
}

function kv_get(string $k, $def = null) {
    $s = db()->prepare('SELECT v FROM kv WHERE k=?');
    $s->execute([$k]);
    $r = $s->fetchColumn();
    return $r === false ? $def : json_decode($r, true);
}

function kv_set(string $k, $v): void {
    db()->prepare('INSERT INTO kv(k,v) VALUES(?,?) ON CONFLICT(k) DO UPDATE SET v=excluded.v')
        ->execute([$k, json_encode($v, JSON_UNESCAPED_UNICODE)]);
}

function out($data, int $code = 200): void {
    http_response_code($code);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store');
    echo json_encode($data, JSON_UNESCAPED_UNICODE);
    exit;
}

function session_start_secure(): void {
    $https = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off')
          || (($_SERVER['HTTP_X_FORWARDED_PROTO'] ?? '') === 'https');
    session_name('pagurus');
    session_set_cookie_params(['lifetime' => 60 * 86400, 'path' => '/', 'secure' => $https,
                               'httponly' => true, 'samesite' => 'Strict']);
    ini_set('session.gc_maxlifetime', (string)(60 * 86400));
    session_start();
}

function logged_in(): bool {
    return !empty($_SESSION['ok']);
}

function client_ip(): string {
    return $_SERVER['REMOTE_ADDR'] ?? '?';
}

// Bremse gegen Passwort-Raten: 5 Fehlversuche je IP in 15 Minuten
function login_blocked(): bool {
    $db = db();
    $db->prepare('DELETE FROM fails WHERE ts<?')->execute([microtime(true) - 900]);
    $s = $db->prepare('SELECT COUNT(*) FROM fails WHERE ip=?');
    $s->execute([client_ip()]);
    return (int)$s->fetchColumn() >= 5;
}

function login_failed(): void {
    db()->prepare('INSERT INTO fails(ip, ts) VALUES(?,?)')->execute([client_ip(), microtime(true)]);
    sleep(1);
}
