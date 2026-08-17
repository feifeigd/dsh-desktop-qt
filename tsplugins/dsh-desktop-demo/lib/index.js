// DSH Desktop demo plugin — compiled from src/index.ts (TypeScript source).
// Shipped pre-built so the bundle works without a build step at install time.
import { defineTool } from '@deepseek-ai/dsh-tools';

export const name = 'dsh-desktop-demo';
export const inject = ['tools'];

const BOOT_AT = Date.now();

export function apply(ctx) {
  ctx.tools.register(
    defineTool({
      name: 'desktop_hello',
      description:
        'Demo tool contributed by the DSH Desktop Qt shell. ' +
        'Returns a greeting and basic desktop runtime info. ' +
        'Use it to verify that a TypeScript plugin bundle is loaded.',
      parameters: {
        who: {
          type: 'string',
          description: 'Name to greet; defaults to "world".',
        },
      },
      output: {
        schema: {
          type: 'object',
          additionalProperties: false,
          properties: {
            greeting: { type: 'string' },
            runtime: { type: 'string' },
            uptimeSeconds: { type: 'number' },
          },
        },
        render: (_args, value) => [
          { type: 'text', text: `desktop_hello says: ${value.greeting}` },
        ],
      },
      async execute(args, exec) {
        const who = args.who && args.who.trim().length > 0 ? args.who.trim() : 'world';
        void exec;
        return {
          greeting: `Hello, ${who}! 来自 DSH Desktop 的 TypeScript 插件 demo。`,
          runtime: `node ${process.version} / ${process.platform}`,
          uptimeSeconds: Math.round((Date.now() - BOOT_AT) / 1000),
        };
      },
    }),
  );
}
