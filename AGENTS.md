# SPEC-Driven Development 约束文档
- 参考 Notion Page ID: 37d0ec7a4b9280bd8c84c922f57f6d3e

# 全局研发环境与行为准则
- 参考 Notion Page ID: 37d0ec7a4b928005aee8d863af5b2518

# 当前项目需求文档
- 参考 Notion Page ID: 3a30ec7a4b9280258859c311773a22c8
- 变更记录 Notion Page ID: 3a30ec7a4b928038b030ff7e9191c7ee

# 当前项目设计文档
- 参考 Notion Page ID: 3a30ec7a4b928082a21ac8b721d2511d
- 变更记录 Notion Page ID: 3a30ec7a4b92809b91e9e06fe72724d4

# graphify

This project has a graphify knowledge graph at graphify-out/.

Rules:
- Before answering architecture or codebase questions, read graphify-out/GRAPH_REPORT.md for god nodes and community structure
- If graphify-out/wiki/index.md exists, navigate it instead of reading raw files
- For cross-module "how does X relate to Y" questions, prefer `graphify query "<question>"`, `graphify path "<A>" "<B>"`, or `graphify explain "<concept>"` over grep — these traverse the graph's EXTRACTED + INFERRED edges instead of scanning files
- After modifying code files in this session, run `graphify update .` to keep the graph current (AST-only, no API cost)
