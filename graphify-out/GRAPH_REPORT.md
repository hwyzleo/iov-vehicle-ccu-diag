# Graph Report - iov-vehicle-cgw-diag  (2026-07-20)

## Corpus Check
- 13 files · ~13,372 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 39 nodes · 30 edges · 4 communities detected
- Extraction: 97% EXTRACTED · 3% INFERRED · 0% AMBIGUOUS · INFERRED: 1 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Community 0|Community 0]]
- [[_COMMUNITY_Community 1|Community 1]]
- [[_COMMUNITY_Community 3|Community 3]]
- [[_COMMUNITY_Community 4|Community 4]]

## God Nodes (most connected - your core abstractions)
1. `DidRegistry()` - 3 edges
2. `parseEcuConfig()` - 3 edges
3. `TEST_F()` - 2 edges
4. `reload()` - 2 edges
5. `loadDidRegistry()` - 2 edges
6. `parseDidEntry()` - 2 edges
7. `DidRegistryTest` - 1 edges
8. `ConfigLoaderTest` - 1 edges

## Surprising Connections (you probably didn't know these)
- `TEST_F()` --calls--> `DidRegistry()`  [INFERRED]
  tests/test_did_registry.cpp → src/did_registry.cpp

## Communities

### Community 0 - "Community 0"
Cohesion: 0.25
Nodes (2): DidRegistry(), reload()

### Community 1 - "Community 1"
Cohesion: 0.6
Nodes (3): loadDidRegistry(), parseDidEntry(), parseEcuConfig()

### Community 3 - "Community 3"
Cohesion: 0.67
Nodes (2): DidRegistryTest, TEST_F()

### Community 4 - "Community 4"
Cohesion: 0.67
Nodes (1): ConfigLoaderTest

## Knowledge Gaps
- **2 isolated node(s):** `DidRegistryTest`, `ConfigLoaderTest`
  These have ≤1 connection - possible missing edges or undocumented components.
- **Thin community `Community 0`** (9 nodes): `buildIndices()`, `did_registry.cpp`, `DidRegistry()`, `findEcuById()`, `findEcuByLogicalAddress()`, `getDidsForEcu()`, `getRegistryVersion()`, `isEcuRegistered()`, `reload()`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 3`** (3 nodes): `test_did_registry.cpp`, `DidRegistryTest`, `TEST_F()`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Community 4`** (3 nodes): `ConfigLoaderTest`, `test_config_loader.cpp`, `TEST_F()`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `DidRegistry()` connect `Community 0` to `Community 3`?**
  _High betweenness centrality (0.034) - this node is a cross-community bridge._
- **Why does `TEST_F()` connect `Community 3` to `Community 0`?**
  _High betweenness centrality (0.026) - this node is a cross-community bridge._
- **What connects `DidRegistryTest`, `ConfigLoaderTest` to the rest of the system?**
  _2 weakly-connected nodes found - possible documentation gaps or missing edges._