import { mkdir, readFile, writeFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const rootDir = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const sourceFile = resolve(rootDir, "src/data/palettes.json");
const publicFile = resolve(rootDir, "public/palettes.json");

const source = await readFile(sourceFile, "utf8");
JSON.parse(source);

let existing = "";
try {
  existing = await readFile(publicFile, "utf8");
} catch {
  existing = "";
}

if (existing !== source) {
  await mkdir(dirname(publicFile), { recursive: true });
  await writeFile(publicFile, source);
  console.log("Synced public/palettes.json from src/data/palettes.json");
} else {
  console.log("public/palettes.json is up to date");
}
