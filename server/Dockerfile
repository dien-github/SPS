FROM node:20-alpine AS builder

WORKDIR /app

COPY package*.json ./
RUN if [ -f package-lock.json ]; then \
      npm ci --omit=dev; \
    else \
      npm install --omit=dev; \
    fi && \
    npm cache clean --force

FROM node:20-alpine AS runner

RUN apk add --no-cache su-exec && \
    addgroup -g 1001 -S nodejs && \
    adduser -S nodejs -u 1001 -G nodejs

WORKDIR /app

COPY --from=builder /app/node_modules ./node_modules
COPY --chown=nodejs:nodejs . .
COPY docker-entrypoint.sh /usr/local/bin/sps-api-entrypoint
RUN chmod +x /usr/local/bin/sps-api-entrypoint && \
    mkdir -p /app/ota/staging && \
    chown -R nodejs:nodejs /app/ota

ENTRYPOINT ["sps-api-entrypoint"]

EXPOSE 3000

CMD ["node", "src/server.js"]
