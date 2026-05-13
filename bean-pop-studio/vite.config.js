import { fileURLToPath } from "node:url";
import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";

const resolveEntry = (path) => fileURLToPath(new URL(path, import.meta.url));

export default defineConfig({
  build: {
    rollupOptions: {
      input: {
        main: resolveEntry("./index.html"),
        community: resolveEntry("./community/index.html"),
      },
    },
  },
  plugins: [vue()],
  server: {
    host: "0.0.0.0",
    port: 4173,
  },
});
