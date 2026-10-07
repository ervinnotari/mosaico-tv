// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Ervin Notari Junior

'use strict';

/* ONVIF discovery replies and the channel search (GetSystemDateAndTime ->
 * GetCapabilities -> GetProfiles -> GetStreamUri), with the device answers
 * played by the fake XHR and parsed by the XML test double. */

const test = require('node:test');
const assert = require('node:assert');
const { load, plain } = require('./harness');

const ENV = (body) => '<?xml version="1.0"?><s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope"><s:Body>' +
  body + '</s:Body></s:Envelope>';

const CLOCK = ENV('<tds:GetSystemDateAndTimeResponse><tds:SystemDateAndTime><tt:UTCDateTime>' +
  '<tt:Time><tt:Hour>12</tt:Hour><tt:Minute>0</tt:Minute><tt:Second>0</tt:Second></tt:Time>' +
  '<tt:Date><tt:Year>2030</tt:Year><tt:Month>1</tt:Month><tt:Day>2</tt:Day></tt:Date>' +
  '</tt:UTCDateTime></tds:SystemDateAndTime></tds:GetSystemDateAndTimeResponse>');

const CAPS = ENV('<tds:GetCapabilitiesResponse><tds:Capabilities><tt:Media>' +
  '<tt:XAddr>http://10.0.0.5/onvif/Media</tt:XAddr></tt:Media></tds:Capabilities></tds:GetCapabilitiesResponse>');

const profile = (token, source, enc, w, h) =>
  `<trt:Profiles token="${token}"><tt:Name>${token}</tt:Name>` +
  `<tt:VideoSourceConfiguration><tt:SourceToken>${source}</tt:SourceToken></tt:VideoSourceConfiguration>` +
  `<tt:VideoEncoderConfiguration><tt:Encoding>${enc}</tt:Encoding><tt:Resolution>` +
  `<tt:Width>${w}</tt:Width><tt:Height>${h}</tt:Height></tt:Resolution></tt:VideoEncoderConfiguration></trt:Profiles>`;

// Two cameras: the first with main and sub H.264, the second only H.265,
// plus a profile without a video source (audio only), which is ignored.
const PROFILES = ENV('<trt:GetProfilesResponse>' +
  profile('sub1', 'src1', 'H264', 352, 240) +
  profile('main1', 'src1', 'H264', 1920, 1080) +
  profile('main2', 'src2', 'H265', 2560, 1440) +
  '<trt:Profiles token="audio"><tt:Name>audio</tt:Name></trt:Profiles>' +
  '</trt:GetProfilesResponse>');

const URI = (token) => ENV('<trt:GetStreamUriResponse><trt:MediaUri>' +
  `<tt:Uri>rtsp://10.0.0.5:554/${token}</tt:Uri></trt:MediaUri></trt:GetStreamUriResponse>`);

const FAULT = (text) => ENV('<s:Fault><s:Code><s:Value>s:Sender</s:Value></s:Code>' +
  `<s:Reason><s:Text xml:lang="en">${text}</s:Text></s:Reason></s:Fault>`);

const DEVICE = { xaddr: 'http://10.0.0.5/onvif/device_service', name: 'DVR', ip: '10.0.0.5' };

function setup() {
  const ctx = load(['onvif.js']);
  ctx.result = null;
  ctx.Onvif.getChannels(DEVICE, 'admin', 'p@ss', (err, res) => {
    ctx.result = { err: err ? plain(err) : null, res: res ? plain(res) : null };
  });
  return ctx;
}

// Answers the pending request whose SOAP body contains `action`.
function answer(ctx, action, status, xml) {
  const xhr = ctx.requests.find((r) => !r.answered && r.body && r.body.includes(action));
  assert.ok(xhr, 'request for ' + action);
  xhr.answered = true;
  xhr.respond(status, xml);
  return xhr;
}

test('discovery reply: name and model from the scopes, address of the same IP', () => {
  const { Onvif } = load(['onvif.js']);
  const xml = ENV('<d:ProbeMatches><d:ProbeMatch><a:EndpointReference><a:Address>urn:uuid:1234</a:Address></a:EndpointReference>' +
    '<d:Scopes>onvif://www.onvif.org/type/video_encoder onvif://www.onvif.org/name/Garage%20DVR ' +
    'onvif://www.onvif.org/hardware/DS_7204</d:Scopes>' +
    '<d:XAddrs>http://192.168.0.9/onvif/device_service http://10.0.0.5/onvif/device_service</d:XAddrs>' +
    '</d:ProbeMatch></d:ProbeMatches>');
  assert.deepStrictEqual(plain(Onvif.parseProbeMatch('10.0.0.5', xml)), {
    ip: '10.0.0.5', xaddr: 'http://10.0.0.5/onvif/device_service',
    name: 'Garage DVR', hardware: 'DS 7204', id: 'urn:uuid:1234'
  });
  // No scopes and an address of another IP: falls back to the IP and the first address.
  const bare = ENV('<d:ProbeMatch><d:XAddrs>http://192.168.0.9/x</d:XAddrs></d:ProbeMatch>');
  assert.deepStrictEqual(plain(Onvif.parseProbeMatch('10.0.0.5', bare)), {
    ip: '10.0.0.5', xaddr: 'http://192.168.0.9/x', name: '10.0.0.5', hardware: '', id: '10.0.0.5'
  });
});

test('channels: one per video source, main = largest H.264, sub = smallest, login in the URL', () => {
  const ctx = setup();
  const clock = answer(ctx, 'GetSystemDateAndTime', 200, CLOCK);
  assert.ok(!clock.body.includes('UsernameToken'), 'the clock is read without login');
  const caps = answer(ctx, 'GetCapabilities', 200, CAPS);
  assert.ok(caps.body.includes('<Username>admin</Username>'), 'then every call has the login');
  // The digest uses the device clock (2030), not the TV clock.
  assert.ok(/<Created [^>]*>2030-01-02T12:00:/.test(caps.body), caps.body);
  const profiles = answer(ctx, 'GetProfiles', 200, PROFILES);
  assert.strictEqual(profiles.url, 'http://10.0.0.5/onvif/Media', 'uses the media service address');
  for (const t of ['sub1', 'main1', 'main2']) answer(ctx, '<ProfileToken>' + t + '<', 200, URI(t));
  assert.deepStrictEqual(ctx.result, {
    err: null,
    res: {
      name: 'DVR',
      channels: [
        { name: 'DVR · Channel 1', main: 'rtsp://admin:p%40ss@10.0.0.5:554/main1',
          sub: 'rtsp://admin:p%40ss@10.0.0.5:554/sub1', width: 1920, height: 1080, codec: 'H264', supported: true },
        { name: 'DVR · Channel 2', main: 'rtsp://admin:p%40ss@10.0.0.5:554/main2',
          sub: 'rtsp://admin:p%40ss@10.0.0.5:554/main2', width: 2560, height: 1440, codec: 'H265', supported: false }
      ]
    }
  });
});

test('a device that does not answer the clock, without media address, single channel', () => {
  const ctx = setup();
  ctx.requests[0].fail();  // no clock: the digest uses the TV time
  answer(ctx, 'GetCapabilities', 200, ENV('<tds:GetCapabilitiesResponse/>'));
  const profiles = answer(ctx, 'GetProfiles', 200, ENV(profile('p', 'src', 'H264', 1280, 720)));
  assert.strictEqual(profiles.url, DEVICE.xaddr, 'falls back to the device address');
  answer(ctx, '<ProfileToken>p<', 200, URI('p'));
  assert.strictEqual(ctx.result.res.channels.length, 1);
  assert.strictEqual(ctx.result.res.channels[0].name, 'DVR');
});

test('wrong login: HTTP 401 or a SOAP fault about the sender is an auth error', () => {
  let ctx = setup();
  answer(ctx, 'GetSystemDateAndTime', 200, CLOCK);
  answer(ctx, 'GetCapabilities', 401, '');
  assert.strictEqual(ctx.result.err.auth, true);
  assert.strictEqual(ctx.result.err.message, 'HTTP 401');

  ctx = setup();
  answer(ctx, 'GetSystemDateAndTime', 200, CLOCK);
  answer(ctx, 'GetCapabilities', 400, FAULT('Sender not Authorized'));
  assert.deepStrictEqual(ctx.result.err, { status: 400, auth: true, message: 'Sender not Authorized' });
});

test('other failures: profiles, no channels, no URLs, network and timeout', () => {
  let ctx = setup();
  answer(ctx, 'GetSystemDateAndTime', 200, CLOCK);
  answer(ctx, 'GetCapabilities', 200, CAPS);
  answer(ctx, 'GetProfiles', 500, FAULT('Action failed'));
  assert.deepStrictEqual(ctx.result.err, { status: 500, auth: false, message: 'Action failed' });

  ctx = setup();
  answer(ctx, 'GetSystemDateAndTime', 200, CLOCK);
  answer(ctx, 'GetCapabilities', 200, CAPS);
  answer(ctx, 'GetProfiles', 200, ENV('<trt:GetProfilesResponse/>'));
  assert.strictEqual(ctx.result.err.message, 'the device did not report any video channel');

  ctx = setup();
  answer(ctx, 'GetSystemDateAndTime', 200, CLOCK);
  answer(ctx, 'GetCapabilities', 200, CAPS);
  answer(ctx, 'GetProfiles', 200, ENV(profile('p', 'src', 'H264', 1280, 720)));
  answer(ctx, '<ProfileToken>p<', 500, FAULT('No stream'));
  assert.strictEqual(ctx.result.err.message, 'the device did not return RTSP URLs');

  ctx = setup();
  answer(ctx, 'GetSystemDateAndTime', 200, CLOCK);
  ctx.requests[1].fail();
  assert.strictEqual(ctx.result.err.message, 'no response from ' + DEVICE.xaddr);

  ctx = setup();
  answer(ctx, 'GetSystemDateAndTime', 200, CLOCK);
  ctx.requests[1].expire();
  assert.strictEqual(ctx.result.err.message, 'timed out talking to ' + DEVICE.xaddr);
});
