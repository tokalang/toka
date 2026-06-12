// Copyright (c) 2025 YiZhonghua<zhyi@dpai.com>. All rights reserved.
// playground/server.js
// A zero-dependency local compilation server for Toka Playground.

const http = require('http');
const fs = require('fs');
const path = require('path');
const { exec } = require('child_process');

const PORT = 3000;
const ROOT_DIR = path.resolve(__dirname, '..');
const TMP_DIR = path.join(ROOT_DIR, 'tmp');

// Ensure tmp directory exists
if (!fs.existsSync(TMP_DIR)) {
    fs.mkdirSync(TMP_DIR, { recursive: true });
}

function setCorsHeaders(res) {
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
}

const server = http.createServer((req, res) => {
    setCorsHeaders(res);

    // Handle preflight OPTIONS request
    if (req.method === 'OPTIONS') {
        res.writeHead(204);
        res.end();
        return;
    }

    if (req.url === '/status' && req.method === 'GET') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ status: 'ready' }));
        return;
    }

    if (req.url === '/compile' && req.method === 'POST') {
        let body = '';
        req.on('data', chunk => {
            body += chunk.toString();
        });

        req.on('end', () => {
            let code = '';
            try {
                const parsed = JSON.parse(body);
                code = parsed.code || '';
            } catch (err) {
                res.writeHead(400, { 'Content-Type': 'text/plain' });
                res.end('Invalid JSON payload');
                return;
            }

            const timestamp = Date.now() + '_' + Math.floor(Math.random() * 1000);
            const sourceFile = path.join(TMP_DIR, `playground_${timestamp}.tk`);
            const outputFile = path.join(TMP_DIR, `playground_${timestamp}.wasm`);

            // Write Toka source code to temp file
            fs.writeFile(sourceFile, code, (err) => {
                if (err) {
                    res.writeHead(500, { 'Content-Type': 'text/plain' });
                    res.end('Failed to write temporary source file');
                    return;
                }

                // Compile to WebAssembly (WASI)
                const compilerPath = path.join(ROOT_DIR, 'build', 'bin', 'tokac');
                const runtimePath = path.join(ROOT_DIR, 'lib', 'sys', 'toka_rt.wasm.o');
                const includePath = path.join(ROOT_DIR, 'lib');
                
                const cmd = `"${compilerPath}" -target wasm32-wasi -I "${includePath}" "${sourceFile}" "${runtimePath}" -o "${outputFile}"`;

                exec(cmd, { cwd: ROOT_DIR }, (compileErr, stdout, stderr) => {
                    // Cleanup source file asynchronously
                    fs.unlink(sourceFile, () => {});

                    if (compileErr || stderr.includes('error[')) {
                        const errMsg = stderr || compileErr.message;
                        res.writeHead(400, { 'Content-Type': 'text/plain' });
                        res.end(errMsg);
                        return;
                    }

                    // Read generated WASM binary
                    fs.readFile(outputFile, (readErr, data) => {
                        // Cleanup WASM file asynchronously
                        fs.unlink(outputFile, () => {});

                        if (readErr) {
                            res.writeHead(500, { 'Content-Type': 'text/plain' });
                            res.end('Compilation succeeded but failed to read WASM output');
                            return;
                        }

                        // Send compiled WASM binary back to client
                        res.writeHead(200, { 'Content-Type': 'application/wasm' });
                        res.end(data);
                    });
                });
            });
        });
        return;
    }

    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('Not Found');
});

server.listen(PORT, () => {
    console.log(`Toka Playground compilation server running at http://localhost:${PORT}`);
});
