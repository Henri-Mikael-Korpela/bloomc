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
    Bool:   'built-in boolean type',
    CStr:   'built-in null-terminated C string type',
    Int:    'built-in signed integer type',
    RawPtr: 'built-in raw (untyped) pointer type, equivalent to void* in C',
    Str:    'built-in UTF-8 string type',
    U8:     'built-in unsigned 8-bit integer type',
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

const BUILTIN_PROC_RETURN_TYPES: Record<string, string> = {
    clone_to_cstr:       'CStr',
    length:              'Int',
    length_in_bytes:     'Int',
    try_parse_int_le:    'Int',
};

// Returns the declared return type of a proc, e.g. "File" or "^FILE" or null.
function lookupProcReturnType(text: string, procName: string): string | null {
    if (BUILTIN_PROC_RETURN_TYPES[procName]) {
        return BUILTIN_PROC_RETURN_TYPES[procName];
    }
    const e = escapeRegex(procName);
    const m = text.match(new RegExp(`\\b${e}\\s*::\\s*proc\\([^)]*\\)[ \\t]*(\\S+)[ \\t]*->`, 'm'));
    if (!m) return null;
    const ret = m[1].trim();
    // Ignore the arrow itself (means no return type)
    if (ret === '->') return null;
    return ret || null;
}

// Returns the type of a field in the named struct, e.g. "Str" or "^FILE".
function lookupStructFieldType(text: string, structName: string, fieldName: string): string | null {
    const sn = escapeRegex(structName);
    // Find the struct body — lines indented after "StructName :: struct ->"
    const structHeaderRe = new RegExp(`\\b${sn}\\s*::\\s*struct\\s*->([\\s\\S]*?)(?=\\n\\S|$)`);
    const structMatch = text.match(structHeaderRe);
    if (!structMatch) return null;

    const fn = escapeRegex(fieldName);
    const fieldRe = new RegExp(`^[ \\t]+${fn}\\s*:\\s*(.+)$`, 'm');
    const fieldMatch = structMatch[1].match(fieldRe);
    if (!fieldMatch) return null;
    return fieldMatch[1].trim();
}

// Counts elements in an array body string (text between the outer { and }).
// Counts top-level commas (not nested inside inner {}) with trailing-comma awareness.
function countArrayElements(body: string): number {
    let depth = 0;
    let commas = 0;
    let sawNonWS = false;
    for (const ch of body) {
        if (ch === '{') { depth++; }
        else if (ch === '}') { depth--; }
        else if (ch === ',' && depth === 0) { commas++; sawNonWS = false; }
        else if (depth === 0 && !/\s/.test(ch)) { sawNonWS = true; }
    }
    if (!sawNonWS && commas === 0) { return 0; }
    return sawNonWS ? commas + 1 : commas;
}

// Extracts the body string between the outer { } of an array initialiser.
// exprLine is the trimmed RHS expression (may be only the first line for multiline arrays).
// Falls back to scanning the full document text when the closing } is not on the same line.
function extractArrayBody(text: string, exprLine: string): string | null {
    const braceIdx = exprLine.indexOf('{');
    if (braceIdx === -1) { return null; }

    // Single-line array: check whether the matching } is already in exprLine
    let depth = 0;
    for (let i = braceIdx; i < exprLine.length; i++) {
        if (exprLine[i] === '{') { depth++; }
        else if (exprLine[i] === '}') {
            depth--;
            if (depth === 0) {
                return exprLine.substring(braceIdx + 1, i);
            }
        }
    }

    // Multi-line: locate the opening { in the full document text
    const searchFor = exprLine.trimEnd();
    const pos = text.indexOf(searchFor);
    if (pos === -1) { return null; }
    const textBraceIdx = text.indexOf('{', pos + braceIdx);
    if (textBraceIdx === -1) { return null; }

    depth = 0;
    for (let i = textBraceIdx; i < text.length; i++) {
        if (text[i] === '{') { depth++; }
        else if (text[i] === '}') {
            depth--;
            if (depth === 0) { return text.substring(textBraceIdx + 1, i); }
        }
    }
    return null;
}

// Infers the Bloom type of an expression string.
// depth guards against infinite recursion when looking up variable types.
function inferExprType(text: string, expr: string, depth: number): string | null {
    if (depth > 4) return null;

    const s = expr.trim();

    // Integer literal (possibly negative)
    if (/^-?\d+$/.test(s)) return 'Int';

    // String literal
    if (/^"[^"]*"$/.test(s)) return 'Str';

    // Boolean literal
    if (s === 'true' || s === 'false') return 'Bool';

    // Array init: [const]Type{ or [N]Type{
    const arrayInitMatch = s.match(/^\[(\w+)\](\w+)\s*\{/);
    if (arrayInitMatch) {
        const lenSpec = arrayInitMatch[1];
        const typeName = arrayInitMatch[2];
        if (/^\d+$/.test(lenSpec)) {
            return `[${lenSpec}]${typeName}`;
        }
        // lenSpec === 'const': count elements from the array body
        const body = extractArrayBody(text, s);
        if (body !== null) {
            return `[const = ${countArrayElements(body)}]${typeName}`;
        }
        return `[const]${typeName}`;
    }

    // Address-of: %varname
    const addrOfMatch = s.match(/^%(\w+)$/);
    if (addrOfMatch) {
        const baseType = inferVarType(text, addrOfMatch[1], depth + 1);
        if (baseType) return `^${baseType}`;
        return null;
    }

    // Proc call: procName(...)
    // Must be checked before member access and struct init (both can look like identifiers).
    const procCallMatch = s.match(/^(\w+)\s*\(/);
    if (procCallMatch) {
        return lookupProcReturnType(text, procCallMatch[1]);
    }

    // Member access: obj.field  (simple, single dot)
    const memberMatch = s.match(/^(\w+)\.(\w+)$/);
    if (memberMatch) {
        const objName = memberMatch[1];
        // Check if objName is an enum type directly (Color.RED syntax)
        if (new RegExp(`\\b${escapeRegex(objName)}\\s*::\\s*enum\\b`).test(text)) {
            return objName;
        }
        const objType = inferVarType(text, objName, depth + 1);
        if (!objType) return null;
        // Strip leading ^ for pointer dereference
        const baseType = objType.startsWith('^') ? objType.slice(1) : objType;
        return lookupStructFieldType(text, baseType, memberMatch[2]);
    }

    // Array access: arr[idx]
    const arrayAccessMatch = s.match(/^(\w+)\[/);
    if (arrayAccessMatch) {
        const arrType = inferVarType(text, arrayAccessMatch[1], depth + 1);
        if (arrType && arrType.startsWith('[]')) return arrType.slice(2);
        return null;
    }

    // Struct init: TypeName { or TypeName{
    const structInitMatch = s.match(/^(\w+)\s*\{/);
    if (structInitMatch) {
        const typeName = structInitMatch[1];
        if (new RegExp(`\\b${escapeRegex(typeName)}\\s*::\\s*struct\\b`).test(text)) {
            return typeName;
        }
    }

    return null;
}

// Finds the definition of `name` in the file and returns its inferred type.
function inferVarType(text: string, name: string, depth: number): string | null {
    if (depth > 4) return null;

    const e = escapeRegex(name);

    // Local variable: name :=
    const varDefRe = new RegExp(`\\b${e}\\s*:=\\s*(.+)$`, 'm');
    const varDefMatch = text.match(varDefRe);
    if (varDefMatch) return inferExprType(text, varDefMatch[1], depth);

    // Constant binding (non-proc, non-struct, non-enum): name :: expr
    const constDefRe = new RegExp(`\\b${e}\\s*::\\s*(?!proc\\b|struct\\b|enum\\b)(.+)$`, 'm');
    const constDefMatch = text.match(constDefRe);
    if (constDefMatch) return inferExprType(text, constDefMatch[1], depth);

    // Proc parameter: look inside proc(...  name: Type ...)
    const paramRe = new RegExp(`proc\\([^)]*\\b${e}\\s*:\\s*([^,)]+)`);
    const paramMatch = text.match(paramRe);
    if (paramMatch) return paramMatch[1].trim();

    // Struct field
    const fieldRe = new RegExp(`^[ \\t]+${e}\\s*:\\s*(.+)$`, 'm');
    const fieldMatch = text.match(fieldRe);
    if (fieldMatch) return fieldMatch[1].trim();

    return null;
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

    // Enum definition: Name :: enum
    if (new RegExp(`\\b${e}\\s*::\\s*enum\\b`).test(text)) {
        return `**enum** \`${name}\``;
    }

    // Constant/value binding: name :: (not proc, not struct, not enum — already excluded above)
    if (new RegExp(`\\b${e}\\s*::`).test(text)) {
        const constDefRe = new RegExp(`\\b${e}\\s*::\\s*(?!proc\\b|struct\\b|enum\\b)(.+)$`, 'm');
        const constDefMatch = text.match(constDefRe);
        const inferredType = constDefMatch
            ? inferExprType(text, constDefMatch[1], 0)
            : null;
        if (inferredType) {
            return `**constant** \`${name}: ${inferredType}\``;
        }
        return `**constant** \`${name}\``;
    }

    // Variable definition: name :=
    const varDefRe = new RegExp(`\\b${e}\\s*:=\\s*(.+)$`, 'm');
    const varDefMatch = text.match(varDefRe);
    if (varDefMatch) {
        const inferredType = inferExprType(text, varDefMatch[1], 0);
        if (inferredType) {
            return `**variable** \`${name}: ${inferredType}\``;
        }
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
