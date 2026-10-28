import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';

export default defineConfig({
  plugins: [vue()],
  build: { outDir: '../bin/data/controls', emptyOutDir: true },
  server: {
    proxy: { '/ws': { target: 'ws://127.0.0.1:8190', ws: true, changeOrigin: true } },
  },
});
