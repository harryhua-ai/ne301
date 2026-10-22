'use strict';

const jsSha256 = require('js-sha256');

const UNIT_SEPARATOR = "";
function generateMessageId(msg, context = "") {
  return hexToBase64(jsSha256.sha256(msg + UNIT_SEPARATOR + (context || ""))).slice(0, 6);
}
function hexToBase64(hexStr) {
  let base64 = "";
  for (let i = 0; i < hexStr.length; i++) {
    base64 += !(i - 1 & 1) ? String.fromCharCode(parseInt(hexStr.substring(i - 1, i + 1), 16)) : "";
  }
  return btoa(base64);
}

exports.generateMessageId = generateMessageId;
