# Graphify

 [Github](https://github.com/Graphify-Labs/graphify)

**Install graphify mcp** (requires python 3.10 and uv)

```bash
uv tool install "graphifyy[mcp]"
```

**Builds the graph**:

```bash
graphify extract . --code-only ; graphify cluster-only . --no-label
```

**Optionally**: install git hooks (commit & checkout)

```bash
graphify hook install
```

> The generated .gitattributes line `graphify-out/graph.json merge=graphify` can
> and should be removed since the graphify files are ignored
