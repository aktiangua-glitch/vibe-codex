import { createServer } from 'node:http'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

import express from 'express'
import { ExpressPeerServer } from 'peer'

const host = process.env.HOST || '0.0.0.0'
const port = Number(process.env.PORT || '3000')

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const distDir = path.join(__dirname, 'dist')
const indexFile = path.join(distDir, 'index.html')

const app = express()
const server = createServer(app)

app.set('trust proxy', true)

const peerServer = ExpressPeerServer(server, {
  proxied: true,
  allow_discovery: false,
  debug: process.env.NODE_ENV !== 'production',
})

app.get('/healthz', (_request, response) => {
  response.json({ ok: true })
})

app.use('/peerjs', peerServer)
app.use(
  express.static(distDir, {
    index: 'index.html',
    maxAge: '1h',
  }),
)

app.use((request, response, next) => {
  if (request.method !== 'GET') {
    next()
    return
  }

  if (request.path.startsWith('/peerjs') || path.extname(request.path)) {
    next()
    return
  }

  response.sendFile(indexFile)
})

const shutdown = (signal) => {
  console.log(`[relay-drop] received ${signal}, shutting down`)
  server.close(() => {
    process.exit(0)
  })
}

process.on('SIGINT', () => shutdown('SIGINT'))
process.on('SIGTERM', () => shutdown('SIGTERM'))

server.listen(port, host, () => {
  console.log(`[relay-drop] listening on http://${host}:${port}`)
})
