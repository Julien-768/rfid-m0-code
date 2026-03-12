#!/usr/bin/env node
"use strict";

const fs = require("fs");
const path = require("path");

// Adresse de départ application (SAMD21 / Feather M0)
const APP_START_ADDRESS = 0x00002000;

// Constantes UF2
const UF2_MAGIC_START0 = 0x0A324655; // "UF2\n"
const UF2_MAGIC_START1 = 0x9E5D5157;
const UF2_MAGIC_END    = 0x0AB16F30;

function usage() {
  console.error("Usage: node tools/bin2uf2.js <input.bin> <output.uf2>");
  process.exit(1);
}

const inPath  = process.argv[2];
const outPath = process.argv[3] || "flash.uf2";
if (!inPath) usage();

// Lecture du .bin
if (!fs.existsSync(inPath)) {
  console.error("Fichier introuvable:", inPath);
  process.exit(1);
}
const buf = fs.readFileSync(inPath);
if (buf.length === 0) {
  console.error("Le fichier .bin est vide.");
  process.exit(1);
}

// Calcul blocs (256 octets de payload / bloc)
const payloadSize = 256;
const blockSize   = 512;
const numBlocks   = Math.ceil(buf.length / payloadSize);

const outBlocks = [];
for (let pos = 0, blockNo = 0; pos < buf.length; pos += payloadSize, blockNo++) {
  const bl = Buffer.alloc(blockSize, 0);

  // En-tête UF2 (little endian)
  bl.writeUInt32LE(UF2_MAGIC_START0, 0);
  bl.writeUInt32LE(UF2_MAGIC_START1, 4);
  bl.writeUInt32LE(0, 8);                               // flags (0 = pas de familyID)
  bl.writeUInt32LE(APP_START_ADDRESS + pos, 12);        // targetAddr
  bl.writeUInt32LE(payloadSize, 16);                    // payloadSize
  bl.writeUInt32LE(blockNo, 20);                        // blockNo
  bl.writeUInt32LE(numBlocks, 24);                      // numBlocks
  bl.writeUInt32LE(0, 28);                              // reserved

  // Payload sécurisé (pad avec 0 si on dépasse la taille du .bin)
  const end = Math.min(pos + payloadSize, buf.length);
  buf.copy(bl, 32, pos, end);

  // Magic de fin
  bl.writeUInt32LE(UF2_MAGIC_END, blockSize - 4);

  outBlocks.push(bl);
}

if (numBlocks !== outBlocks.length) {
  console.error("Incohérence nombre de blocs UF2.");
  process.exit(1);
}

// Écriture du .uf2
const outAbs = path.resolve(outPath);
fs.writeFileSync(outAbs, Buffer.concat(outBlocks));
console.log(`Wrote ${numBlocks} blocks to ${outAbs}`);
