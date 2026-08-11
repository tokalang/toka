# Toka LLM chat CLI

A minimal interactive, streaming CLI using DeepSeek's OpenAI-compatible
chat-completions endpoint. It keeps conversation history only in memory.

```sh
cmake --build build --parallel 2
python3 demos/llm-chat-cli/qualify.py
```

The qualification resolves the immutable public
`official/openai_compat@0.1.1` fixture before compiling the demo, so it does
not depend on a monorepo package directory.

It requires `DEEPSEEK_API_KEY`. By default it uses:

```text
DEEPSEEK_BASE_URL=https://api.deepseek.com
DEEPSEEK_MODEL=deepseek-v4-flash
```

`DEEPSEEK_BASE_URL` is an API base URL; the demo adds `/chat/completions` when
it has no path. A complete compatible HTTPS endpoint path is also accepted.
Set either optional variable to use a compatible endpoint or model.
The key is read only from the environment, placed in the Authorization header,
and never printed or persisted. Use `/reset` to clear in-memory history and
`/quit` to exit.

`--help` is credential-free and prints the local usage text.
