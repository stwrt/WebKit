//@ skip if !$isWasmPlatform
//@ skip if $memoryLimited
//@ runDefault("--maximumWasmDepthForInlining=10", "--maximumWasmCalleeSizeForInlining=10000000", "--maximumWasmCallerSizeForInlining=10000000", "--useBBQJIT=0")
var wasm_code = read('simple-inline-stacktrace.wasm', 'binary')
var wasm_module = new WebAssembly.Module(wasm_code);
var wasm_instance = new WebAssembly.Instance(wasm_module, { a: { doThrow: () => { throw new Error() } } });
var f = wasm_instance.exports.main;
for (let i = 0; i < wasmTestLoopCount; ++i) {
    try {
        f()
    } catch (e) {
        let str = e.stack.toString()
        let trace = str.split('\n')
        let expected = ["*", "<?>.wasm-function[g]@[wasm code]",
        "<?>.wasm-function[f]@[wasm code]", "<?>.wasm-function[e]@[wasm code]", "<?>.wasm-function[d]@[wasm code]",
        "<?>.wasm-function[c]@[wasm code]", "<?>.wasm-function[b]@[wasm code]", "<?>.wasm-function[a]@[wasm code]",
        "<?>.wasm-function[main]@[wasm code]", "*"]
        if (trace.length != expected.length)
            throw "unexpected length"
        for (let i = 0; i < trace.length; ++i) {
            if (expected[i] == "*")
                continue
            if (expected[i] != trace[i].trim())
                throw "mismatch at " + i
        }
    }
}

let mem = new Int32Array(wasm_instance.exports.mem.buffer)[0]
if (mem != wasmTestLoopCount)
    throw `Expected ${wasmTestLoopCount}, got ${mem}`;
