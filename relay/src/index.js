// Grand Theft ASCII relay (Cloudflare Worker + Durable Objects).
//
// Online rooms are hosted by a player's own game. The host and every guest
// keep one WebSocket to this relay; the relay forwards bytes between them, so
// nobody needs a public IP or port forwarding.
//
//   GET /            plain-text banner
//   GET /rooms       JSON list of public rooms (also as a WebSocket: one text message, then close)
//   WS  /host?code=ABCD&name=..&mode=0&max=8&public=1    become the host of room ABCD
//   WS  /join?code=ABCD                                  join room ABCD
//
// Binary messages carry one or more records: [u8 kind][u16 peer][u16 length][payload]
// (little endian). `peer` identifies a guest on the host's connection and is 0
// on a guest's connection.

const KIND_TCP = 1;   // one game TCP frame: [u8 type][payload]
const KIND_UDP = 2;   // one game datagram
const KIND_OPEN = 3;  // relay -> host: a guest connected
const KIND_CLOSE = 4; // relay -> host: guest left; host -> relay: kick guest
const KIND_READY = 5; // relay -> client: registered / joined (payload: room code)

const CODE_RE = /^[A-Z0-9]{4,8}$/;
const LOBBY_TTL_MS = 11 * 60 * 1000;
const REFRESH_MS = 5 * 60 * 1000;

function record(kind, peer, payload) {
  const body = payload || new Uint8Array(0);
  const out = new Uint8Array(5 + body.length);
  out[0] = kind;
  out[1] = peer & 0xff;
  out[2] = peer >> 8;
  out[3] = body.length & 0xff;
  out[4] = body.length >> 8;
  out.set(body, 5);
  return out;
}

function* records(bytes) {
  let at = 0;
  while (at + 5 <= bytes.length) {
    const kind = bytes[at];
    const peer = bytes[at + 1] | (bytes[at + 2] << 8);
    const len = bytes[at + 3] | (bytes[at + 4] << 8);
    if (at + 5 + len > bytes.length) return;
    yield { kind, peer, start: at, end: at + 5 + len };
    at += 5 + len;
  }
}

function concat(parts) {
  let n = 0;
  for (const p of parts) n += p.length;
  const out = new Uint8Array(n);
  let at = 0;
  for (const p of parts) {
    out.set(p, at);
    at += p.length;
  }
  return out;
}

const text = (body, status = 200) =>
  new Response(body, { status, headers: { 'content-type': 'text/plain; charset=utf-8' } });

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const upgrade = request.headers.get('Upgrade') === 'websocket';
    if (url.pathname === '/rooms') {
      const list = await env.LOBBY.get(env.LOBBY.idFromName('lobby')).fetch('https://lobby/list');
      const body = await list.text();
      if (!upgrade) return new Response(body, { headers: { 'content-type': 'application/json' } });
      const [client, server] = Object.values(new WebSocketPair());
      server.accept();
      server.send(body);
      server.close(1000, 'done');
      return new Response(null, { status: 101, webSocket: client });
    }
    if (url.pathname === '/host' || url.pathname === '/join') {
      if (!upgrade) return text('WebSocket required', 426);
      const code = (url.searchParams.get('code') || '').toUpperCase();
      if (!CODE_RE.test(code)) return text('Bad room code', 400);
      return env.ROOMS.get(env.ROOMS.idFromName(code)).fetch(request);
    }
    return text('Grand Theft ASCII relay. Play with: npx gtascii\n');
  },
};

// One instance per room code.
export class Room {
  constructor(ctx, env) {
    this.ctx = ctx;
    this.env = env;
    // Keep-alives are answered without waking the object (and are not billed).
    ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair('ping', 'pong'));
  }

  host() {
    return this.ctx.getWebSockets('host')[0] || null;
  }

  guests() {
    return this.ctx.getWebSockets('guest');
  }

  guest(peer) {
    return this.ctx.getWebSockets('g' + peer)[0] || null;
  }

  async meta() {
    return (await this.ctx.storage.get('meta')) || null;
  }

  async fetch(request) {
    const url = new URL(request.url);
    const code = url.searchParams.get('code').toUpperCase();
    const [client, server] = Object.values(new WebSocketPair());

    if (url.pathname === '/host') {
      if (this.host()) return text('Room code already in use', 409);
      const p = url.searchParams;
      const meta = {
        code,
        name: (p.get('name') || 'Room').slice(0, 24),
        mode: Math.max(0, Math.min(2, parseInt(p.get('mode') || '0', 10) || 0)),
        max: Math.max(2, Math.min(16, parseInt(p.get('max') || '8', 10) || 8)),
        isPublic: p.get('public') !== '0',
        nextPeer: 1,
      };
      await this.ctx.storage.put('meta', meta);
      this.ctx.acceptWebSocket(server, ['host']);
      server.send(record(KIND_READY, 0, new TextEncoder().encode(code)));
      await this.publish();
      await this.ctx.storage.setAlarm(Date.now() + REFRESH_MS);
      return new Response(null, { status: 101, webSocket: client });
    }

    // join
    const host = this.host();
    const meta = await this.meta();
    if (!host || !meta) return text('No room with code ' + code + ' (the host may have left)', 404);
    if (this.guests().length + 1 >= meta.max) return text('Room ' + code + ' is full', 403);
    let peer = meta.nextPeer;
    while (this.guest(peer) || peer === 0) peer = (peer % 65535) + 1;
    meta.nextPeer = (peer % 65535) + 1;
    await this.ctx.storage.put('meta', meta);
    this.ctx.acceptWebSocket(server, ['guest', 'g' + peer]);
    server.serializeAttachment({ peer });
    server.send(record(KIND_READY, 0, new TextEncoder().encode(code)));
    host.send(record(KIND_OPEN, peer));
    await this.publish();
    return new Response(null, { status: 101, webSocket: client });
  }

  async webSocketMessage(ws, message) {
    if (typeof message === 'string') return; // stray text (keep-alive variants)
    const bytes = new Uint8Array(message);
    const tags = this.ctx.getTags(ws);
    if (tags.includes('host')) {
      // Split the host's batch per guest; one message per guest.
      const perGuest = new Map();
      for (const r of records(bytes)) {
        if (r.kind === KIND_CLOSE) {
          const g = this.guest(r.peer);
          if (g) g.close(4000, 'Removed by the host');
          continue;
        }
        if (r.kind !== KIND_TCP && r.kind !== KIND_UDP) continue;
        if (!perGuest.has(r.peer)) perGuest.set(r.peer, []);
        const part = bytes.slice(r.start, r.end);
        part[1] = 0;
        part[2] = 0;
        perGuest.get(r.peer).push(part);
      }
      for (const [peer, parts] of perGuest) {
        const g = this.guest(peer);
        if (g) {
          try {
            g.send(concat(parts));
          } catch (_) { /* guest just left */ }
        }
      }
      return;
    }
    // From a guest: stamp its peer id on every record and pass it to the host.
    const { peer } = ws.deserializeAttachment() || {};
    const host = this.host();
    if (!host || !peer) return;
    for (const r of records(bytes)) {
      bytes[r.start + 1] = peer & 0xff;
      bytes[r.start + 2] = peer >> 8;
    }
    try {
      host.send(bytes);
    } catch (_) { /* host just left */ }
  }

  async webSocketClose(ws) {
    await this.closed(ws);
  }

  async webSocketError(ws) {
    await this.closed(ws);
  }

  async closed(ws) {
    const tags = this.ctx.getTags(ws);
    if (tags.includes('host')) {
      for (const g of this.guests()) {
        try {
          g.close(4001, 'The host closed the room');
        } catch (_) { /* already gone */ }
      }
      const meta = await this.meta();
      await this.ctx.storage.deleteAlarm();
      await this.ctx.storage.deleteAll();
      if (meta) await this.lobby({ remove: meta.code });
      return;
    }
    const { peer } = ws.deserializeAttachment() || {};
    const host = this.host();
    if (host && peer) {
      try {
        host.send(record(KIND_CLOSE, peer));
      } catch (_) { /* host gone */ }
    }
    await this.publish();
  }

  async alarm() {
    if (!this.host()) {
      await this.ctx.storage.deleteAll();
      return;
    }
    await this.publish();
    await this.ctx.storage.setAlarm(Date.now() + REFRESH_MS);
  }

  // Tells the lobby about this room (public rooms only).
  async publish() {
    const meta = await this.meta();
    if (!meta) return;
    if (!this.host() || !meta.isPublic) {
      await this.lobby({ remove: meta.code });
      return;
    }
    await this.lobby({
      put: { code: meta.code, name: meta.name, mode: meta.mode, players: this.guests().length + 1, max: meta.max },
    });
  }

  async lobby(body) {
    await this.env.LOBBY.get(this.env.LOBBY.idFromName('lobby')).fetch('https://lobby/update', {
      method: 'POST',
      body: JSON.stringify(body),
    });
  }
}

// A single instance: the list of public rooms.
export class Lobby {
  constructor(ctx) {
    this.ctx = ctx;
    ctx.storage.sql.exec(
      'CREATE TABLE IF NOT EXISTS rooms (code TEXT PRIMARY KEY, name TEXT, mode INTEGER, players INTEGER, max INTEGER, updated INTEGER)'
    );
  }

  async fetch(request) {
    const url = new URL(request.url);
    const sql = this.ctx.storage.sql;
    if (url.pathname === '/update') {
      const body = await request.json();
      if (body.put) {
        const r = body.put;
        sql.exec(
          'INSERT OR REPLACE INTO rooms (code, name, mode, players, max, updated) VALUES (?, ?, ?, ?, ?, ?)',
          r.code, r.name, r.mode, r.players, r.max, Date.now()
        );
      } else if (body.remove) {
        sql.exec('DELETE FROM rooms WHERE code = ?', body.remove);
      }
      return text('ok');
    }
    sql.exec('DELETE FROM rooms WHERE updated < ?', Date.now() - LOBBY_TTL_MS);
    const rows = sql.exec('SELECT code, name, mode, players, max FROM rooms ORDER BY players DESC, updated DESC LIMIT 50').toArray();
    return new Response(JSON.stringify(rows), { headers: { 'content-type': 'application/json' } });
  }
}
