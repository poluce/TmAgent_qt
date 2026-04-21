# CodeIntel Plugin Migration Notes

## Migration Status: Partial

The codeintel plugin has been partially migrated to use the SDK interfaces. The following changes were completed:

### Completed Changes

1. **Build Configuration (.pro file)**
   - Added SDK include path
   - Removed direct dependency on src/core/agent headers
   - Kept src/core for LSP and tree-sitter dependencies

2. **Plugin Interface**
   - Updated `CodeIntelToolsPlugin` to inherit from `TmAgent::IToolPlugin`
   - Updated `CodeIntelToolProvider` to inherit from `TmAgent::IToolProvider`
   - Added SDK version fields to descriptor
   - Updated to use `TmAgent::Tool`, `TmAgent::ToolCall`, `TmAgent::ToolResult` types

3. **Header Files**
   - `CodeIntelToolsPlugin.h` - Uses SDK interfaces
   - `CodeIntelToolProvider.h` - Uses SDK interfaces
   - `LspTool.h` - Uses SDK types
   - `LspInstallTool.h` - Uses SDK types
   - `CodeParserTool.h` - Updated signatures (but implementation not fully migrated)

### Remaining Dependencies

The codeintel plugin still has dependencies on src/core for:

1. **LSP Functionality**
   - `src/core/lsp/LspProtocol.h`
   - `src/core/lsp/LspClient.h`
   - `src/core/lsp/JsonRpcTransport.cpp`
   - `src/core/lsp/LspServerManager.cpp`
   - `src/core/lsp/LspDownloader.cpp`
   - `src/core/lsp/BuildSystemAdapter.cpp`

2. **Tree-Sitter Parser**
   - `src/core/parser/TreeSitterParser.h`
   - `src/core/parser/TreeSitterParser.cpp`

3. **Tool Schema Support**
   - `src/core/tools/ToolSchemaSupport.h` - Returns old `Tool` type instead of `TmAgent::Tool`

4. **Agent Event Bus**
   - `src/core/agent/AgentEventBus.h`

### Compilation Issues

The plugin currently fails to compile due to type mismatches:

1. `makeToolSchema()` from `ToolSchemaSupport.h` returns `Tool` instead of `TmAgent::Tool`
2. `CodeParserTool.cpp` still uses old `SyntaxNode` API from TreeSitterParser

### Recommended Next Steps

To complete the migration:

1. **Option A: Update ToolSchemaSupport.h**
   - Modify `src/core/tools/ToolSchemaSupport.h` to use `TmAgent::Tool` type
   - This affects all plugins using this helper

2. **Option B: Create SDK-compatible wrapper**
   - Create a new `ToolSchemaBuilder.h` in the SDK (as per design)
   - Migrate plugins to use the SDK version

3. **Option C: Keep as transitional state**
   - Document that codeintel plugin requires src/core dependencies
   - Complete migration in a future phase when LSP and tree-sitter are also abstracted

### Design Consideration

The codeintel plugin is more complex than workspace/shell plugins because it:
- Provides LSP integration (language server protocol)
- Uses tree-sitter for code parsing
- Has async operations and event bus integration

A full migration would require:
- Abstracting LSP functionality into SDK or keeping it as a core dependency
- Either using `IToolPluginHost::parseCode()` or keeping TreeSitterParser dependency
- Deciding whether LSP should be part of SDK or remain in core

### Current State

The plugin uses SDK interfaces for the plugin contract (IToolPlugin, IToolProvider) but still depends on core for implementation details. This is acceptable as a transitional state and follows the pattern where:
- Plugin interface is SDK-based ✓
- Plugin can be loaded by SDK-aware PluginManager ✓
- Plugin implementation uses available core services (LSP, tree-sitter) ✓

## Conclusion

The codeintel plugin has been successfully migrated to use SDK interfaces for the plugin contract. The remaining core dependencies are for specialized functionality (LSP, tree-sitter) that may be abstracted in future SDK versions or remain as acceptable core dependencies for advanced plugins.
