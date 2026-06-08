import {
    createConnection,
    TextDocuments,
    ProposedFeatures,
    InitializeParams,
    InitializeResult,
    TextDocumentSyncKind,
    HoverParams,
    Hover,
    MarkupKind,
    Position,
    Range,
} from 'vscode-languageserver/node';
import { TextDocument } from 'vscode-languageserver-textdocument';

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);

connection.onInitialize((_params: InitializeParams): InitializeResult => {
    return {
        capabilities: {
            textDocumentSync: TextDocumentSyncKind.Incremental,
            hoverProvider: true,
        },
    };
});

const BUILTIN_TYPES: Record<string, string> = {
    Bool: 'built-in boolean type',
    CStr: 'built-in null-terminated C string type',
    Int:  'built-in signed integer type',
    Str:  'built-in UTF-8 string type',
    U8:   'built-in unsigned 8-bit integer type',
};

function escapeRegex(s: string): string {
    return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function getWordAtPosition(
    text: string,
    position: Position,
): { word: string; range: Range } | null {
    const lines = text.split('\n');
    const line = lines[position.line] ?? '';
    const char = position.character;

    const isWordChar = (c: string): boolean => /[a-zA-Z0-9_]/.test(c);

    if (char >= line.length || !isWordChar(line[char])) return null;

    let start = char;
    let end = char;
    while (start > 0 && isWordChar(line[start - 1])) start--;
    while (end < line.length && isWordChar(line[end])) end++;

    return {
        word: line.substring(start, end),
        range: {
            start: { line: position.line, character: start },
            end: { line: position.line, character: end },
        },
    };
}

function analyzeIdentifier(text: string, name: string): string | null {
    if (BUILTIN_TYPES[name]) {
        return `**type** \`${name}\` — ${BUILTIN_TYPES[name]}`;
    }

    if (name === 'context') {
        return '**variable** `context` — implicit procedure context (`BloomContext`)';
    }

    const e = escapeRegex(name);

    // Procedure definition: name :: proc(params) ReturnType ->
    const procMatch = text.match(
        new RegExp(`\\b${e}\\s*::\\s*proc\\(([^)]*)\\)[ \\t]*(.*?)[ \\t]*->`, 'm'),
    );
    if (procMatch) {
        const params = procMatch[1].trim();
        const returnType = procMatch[2].trim();
        const sig = returnType
            ? `${name} :: proc(${params}) ${returnType}`
            : `${name} :: proc(${params})`;
        return `**procedure**\n\`\`\`\n${sig}\n\`\`\``;
    }

    // Struct definition: Name :: struct
    if (new RegExp(`\\b${e}\\s*::\\s*struct\\b`).test(text)) {
        return `**struct** \`${name}\``;
    }

    // Constant/value binding: name :: (not proc, not struct — already excluded above)
    if (new RegExp(`\\b${e}\\s*::`).test(text)) {
        return `**constant** \`${name}\``;
    }

    // Variable definition: name :=
    if (new RegExp(`\\b${e}\\s*:=`).test(text)) {
        return `**variable** \`${name}\``;
    }

    // Procedure parameter: name: Type inside proc(...)
    const paramMatch = text.match(
        new RegExp(`proc\\([^)]*\\b${e}\\s*:\\s*([^,)]+)`),
    );
    if (paramMatch) {
        return `**parameter** \`${name}: ${paramMatch[1].trim()}\``;
    }

    // Struct field: indented "    name: Type"
    const fieldMatch = text.match(
        new RegExp(`^[ \\t]+${e}\\s*:\\s*(.+)$`, 'm'),
    );
    if (fieldMatch) {
        return `**field** \`${name}: ${fieldMatch[1].trim()}\``;
    }

    return null;
}

connection.onHover((params: HoverParams): Hover | null => {
    const document = documents.get(params.textDocument.uri);
    if (!document) return null;

    const text = document.getText();
    const result = getWordAtPosition(text, params.position);
    if (!result) return null;

    const info = analyzeIdentifier(text, result.word);
    if (!info) return null;

    return {
        contents: { kind: MarkupKind.Markdown, value: info },
        range: result.range,
    };
});

documents.listen(connection);
connection.listen();
