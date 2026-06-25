import { fileURLToPath } from "node:url";
import { defineConfig } from "vite";

const resolveEntry = (path) => fileURLToPath(new URL(path, import.meta.url));

export default defineConfig({
  build: {
    emptyOutDir: false,
    lib: {
      entry: resolveEntry("./src/pindouPatternSkillEntry.js"),
      fileName: () => "pindou-pattern-skill.js",
      formats: ["iife"],
      name: "PindouPatternSkillBundle",
    },
    minify: "esbuild",
    outDir: "public/skill",
    rollupOptions: {
      output: {
        inlineDynamicImports: true,
      },
    },
  },
  publicDir: false,
});
