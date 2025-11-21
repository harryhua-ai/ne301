function arrayBufferToBase64(buf: ArrayBuffer): string {
    const bytes = new Uint8Array(buf);
    let binary = '';
    for (let i = 0; i < bytes.byteLength; i++) binary += String.fromCharCode(bytes[i]);
    // btoa expects latin1 string
    return btoa(binary);
}

// Detect if file content is PEM format
function isPemFormat(content: string): boolean {
    return content.includes('-----BEGIN') && content.includes('-----END');
}
async function readCertificateFile(file: File): Promise<string> {
    if (file.size === 0) {
        return '';
    }
    const ext = file.name.toLowerCase().slice(file.name.lastIndexOf('.') + 1);

    // Text certificate files (pem, key, txt): return original string directly
    const textExtensions = ['pem', 'key', 'txt'];
    if (textExtensions.includes(ext)) {
        return new Promise((resolve, reject) => {
            const reader = new FileReader();
            reader.onerror = () => reject(reader.error);
            reader.onload = () => {
                const text = reader.result as string;
                // Replace \r\n with \n
                const normalizedText = text.replace(/\r\n/g, '\n');
                resolve(normalizedText);
            };
            reader.readAsText(file);
        });
    }

    // Certificate files (crt, cer, der): intelligent format detection
    if (ext === 'crt' || ext === 'cer' || ext === 'der') {
        return new Promise((resolve, reject) => {
            // First try reading as text to check if it's PEM format
            const textReader = new FileReader();
            textReader.onerror = () => {
                // If text reading fails, process as binary
                const binaryReader = new FileReader();
                binaryReader.onerror = () => reject(binaryReader.error);
                binaryReader.onload = () => {
                    const buffer = binaryReader.result as ArrayBuffer;
                    const base64 = arrayBufferToBase64(buffer);
                    const wrapped = base64.replace(/(.{64})/g, '$1\n');
                    const pem = `-----BEGIN CERTIFICATE-----\n${wrapped}\n-----END CERTIFICATE-----\n`;
                    resolve(pem);
                };
                binaryReader.readAsArrayBuffer(file);
            };
            textReader.onload = () => {
                const content = textReader.result as string;
                if (isPemFormat(content)) {
                    // Already PEM format, return directly and normalize line breaks
                    const normalizedText = content.replace(/\r\n/g, '\n');
                    resolve(normalizedText);
                } else {
                    // Not PEM format, process as binary
                    const binaryReader = new FileReader();
                    binaryReader.onerror = () => reject(binaryReader.error);
                    binaryReader.onload = () => {
                        const buffer = binaryReader.result as ArrayBuffer;
                        const base64 = arrayBufferToBase64(buffer);
                        const wrapped = base64.replace(/(.{64})/g, '$1\n');
                        const pem = `-----BEGIN CERTIFICATE-----\n${wrapped}\n-----END CERTIFICATE-----\n`;
                        resolve(pem);
                    };
                    binaryReader.readAsArrayBuffer(file);
                }
            };
            textReader.readAsText(file);
        });
    }

    // PKCS#12 files (pfx): convert to PEM format
    if (ext === 'pfx') {
        return new Promise((resolve, reject) => {
            const reader = new FileReader();
            reader.onerror = () => reject(reader.error);
            reader.onload = () => {
                const base64 = arrayBufferToBase64(reader.result as ArrayBuffer);
                // Line break every 64 characters, conforming to PEM specification
                const wrapped = base64.replace(/(.{64})/g, '$1\n');
                const pem = `-----BEGIN PKCS12-----\n${wrapped}\n-----END PKCS12-----\n`;
                resolve(pem);
            };
            reader.readAsArrayBuffer(file);
        });
    }

    // PKCS#7 certificate chain files (p7b): convert to PEM format
    if (ext === 'p7b') {
        return new Promise((resolve, reject) => {
            const reader = new FileReader();
            reader.onerror = () => reject(reader.error);
            reader.onload = () => {
                const base64 = arrayBufferToBase64(reader.result as ArrayBuffer);
                // Line break every 64 characters, conforming to PEM specification
                const wrapped = base64.replace(/(.{64})/g, '$1\n');
                const pem = `-----BEGIN PKCS7-----\n${wrapped}\n-----END PKCS7-----\n`;
                resolve(pem);
            };
            reader.readAsArrayBuffer(file);
        });
    }

    // Java KeyStore files (jks): convert to PEM format
    if (ext === 'jks') {
        return new Promise((resolve, reject) => {
            const reader = new FileReader();
            reader.onerror = () => reject(reader.error);
            reader.onload = () => {
                const base64 = arrayBufferToBase64(reader.result as ArrayBuffer);
                // Line break every 64 characters, conforming to PEM specification
                const wrapped = base64.replace(/(.{64})/g, '$1\n');
                const pem = `-----BEGIN KEYSTORE-----\n${wrapped}\n-----END KEYSTORE-----\n`;
                resolve(pem);
            };
            reader.readAsArrayBuffer(file);
        });
    }

    // Other file types: read as text
    return new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.onerror = () => reject(reader.error);
        reader.onload = () => {
            const text = reader.result as string;
            // Replace \r\n with \n
            const normalizedText = text.replace(/\r\n/g, '\n');
            resolve(normalizedText);
        };
        reader.readAsText(file);
    });
}

  export { readCertificateFile };