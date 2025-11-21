/**
 * @file static_to_c_converter.js
 * @brief Static file to C array converter
 * @version 1.0
 * @date 2025
 */

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

class StaticToCConverter {
  constructor(options = {}) {
    this.config = {
      inputDir: options.inputDir || './dist',
      outputDir: options.outputDir || './firmware_assets',
      maxFileSize: options.maxFileSize || 1024 * 1024, // 1MB
      enableCompression: options.enableCompression !== false,
      chunkSize: options.chunkSize || 1024, // Bytes per line
      ...options
    };
    
    this.assets = [];
    this.stats = {
      totalFiles: 0,
      totalSize: 0,
      compressedSize: 0,
      conversionTime: 0
    };
  }

  /**
   * Execute conversion process
   */
  async convert() {
    const startTime = Date.now();
    
    console.log('🚀 Starting static file to C array conversion...');
    console.log(`📁 Input directory: ${this.config.inputDir}`);
    console.log(`📁 Output directory: ${this.config.outputDir}`);
    
    try {
      // 1. Validate input directory
      this.validateInputDirectory();
      
      // 2. Collect all static files
      await this.collectStaticFiles();
      
      // 3. Generate C source files
      await this.generateCSourceFiles();
      
      // 4. Generate resource index
      this.generateResourceIndex();
      
      // 5. Generate firmware config
      this.generateFirmwareConfig();
      
      // 6. Generate binary file
      await this.generateBinaryFile();
      
      // 7. Output statistics
      this.stats.conversionTime = Date.now() - startTime;
      this.printStats();
      
      console.log('✅ Static file to C array conversion completed!');
      
    } catch (error) {
      console.error('❌ Conversion failed:', error);
      throw error;
    }
  }

  /**
   * Validate input directory
   */
  validateInputDirectory() {
    if (!fs.existsSync(this.config.inputDir)) {
      throw new Error(`Input directory does not exist: ${this.config.inputDir}`);
    }
    
    const stat = fs.statSync(this.config.inputDir);
    if (!stat.isDirectory()) {
      throw new Error(`Input path is not a directory: ${this.config.inputDir}`);
    }
    
    console.log('✅ Input directory validation passed');
  }

  /**
   * Collect all static files
   */
  async collectStaticFiles() {
    console.log('📋 Collecting static files...');
    
    this.assets = [];
    await this.walkDirectory(this.config.inputDir, '');
    
    // Sort by file size
    this.assets.sort((a, b) => b.size - a.size);
    
    console.log(`📊 Collected ${this.assets.length} files`);
    
    // Check file size limits
    const oversizedFiles = this.assets.filter(asset => asset.size > this.config.maxFileSize);
    if (oversizedFiles.length > 0) {
      console.warn('⚠️  Found oversized files:');
      oversizedFiles.forEach(file => {
        console.warn(`    ${file.path}: ${(file.size / 1024 / 1024).toFixed(2)} MB`);
      });
    }
  }

  /**
   * Recursively traverse directory
   */
  async walkDirectory(dir, basePath) {
    const files = fs.readdirSync(dir);
    
    for (const file of files) {
      const filePath = path.join(dir, file);
      const stat = fs.statSync(filePath);
      
      if (stat.isDirectory()) {
        // Recursively process subdirectories
        await this.walkDirectory(filePath, path.join(basePath, file));
      } else {
        // Process file
        await this.processFile(filePath, basePath, file);
      }
    }
  }

  /**
   * Process single file
   */
  async processFile(filePath, basePath, fileName) {
    const relativePath = path.join(basePath, fileName);
    const content = fs.readFileSync(filePath);
    const mimeType = this.getMimeType(fileName);
    const hash = this.calculateHash(content);
    
    // Compress content (if enabled)
    let compressedContent = content;
    let compressionRatio = 1.0;
    
    if (this.config.enableCompression && this.isCompressible(mimeType)) {
      const zlib = require('zlib');
      compressedContent = await this.compressBuffer(content);
      compressionRatio = compressedContent.length / content.length;
    }
    
    const asset = {
      path: relativePath,
      originalSize: content.length,
      size: compressedContent.length,
      content: compressedContent,
      mimeType: mimeType,
      hash: hash,
      compressionRatio: compressionRatio,
      isCompressed: compressionRatio < 1.0
    };
    
    this.assets.push(asset);
    this.stats.totalFiles++;
    this.stats.totalSize += content.length;
    this.stats.compressedSize += compressedContent.length;
  }

  /**
   * Compress buffer
   */
  async compressBuffer(buffer) {
    return new Promise((resolve, reject) => {
      const zlib = require('zlib');
      zlib.gzip(buffer, { level: 9 }, (err, result) => {
        if (err) {
          reject(err);
        } else {
          resolve(result);
        }
      });
    });
  }

  /**
   * Determine if file is compressible
   */
  isCompressible(mimeType) {
    const compressibleTypes = [
      'text/',
      'application/javascript',
      'application/json',
      'application/xml',
      'application/xhtml+xml'
    ];
    
    return compressibleTypes.some(type => mimeType.startsWith(type));
  }

  /**
   * Generate C source files
   */
  async generateCSourceFiles() {
    console.log('📝 Generating C source files...');
    
    // Ensure output directory exists
    if (!fs.existsSync(this.config.outputDir)) {
      fs.mkdirSync(this.config.outputDir, { recursive: true });
    }
    
    // Generate header file
    const headerContent = this.generateHeaderFile();
    fs.writeFileSync(path.join(this.config.outputDir, 'web_assets.h'), headerContent);
    
    // Generate source file
    const sourceContent = await this.generateSourceFile();
    fs.writeFileSync(path.join(this.config.outputDir, 'web_assets.c'), sourceContent);
    
    console.log('✅ C source files generation completed');
  }

  /**
   * Generate header file
   */
  generateHeaderFile() {
    const totalSize = this.assets.reduce((sum, asset) => sum + asset.size, 0);
    const compressedSize = this.assets.reduce((sum, asset) => sum + asset.size, 0);
    
    return `/**
 * @file web_assets.h
 * @brief Web frontend resource header file
 * @auto-generated by static_to_c_converter.js
 * @date ${new Date().toISOString()}
 */

#ifndef WEB_ASSETS_H
#define WEB_ASSETS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Resource file structure
typedef struct {
    const char* path;           // File path
    const uint8_t* data;        // File data
    size_t size;               // File size
    const char* mime_type;      // MIME type
    uint32_t hash;             // File hash
    bool is_compressed;         // Whether compressed
    float compression_ratio;    // Compression ratio
} web_asset_t;

// Resource statistics
#define WEB_ASSET_COUNT ${this.assets.length}
#define WEB_TOTAL_SIZE ${totalSize}
#define WEB_COMPRESSED_SIZE ${compressedSize}
#define WEB_COMPRESSION_RATIO ${(compressedSize / totalSize).toFixed(3)}

// Resource file array
extern const web_asset_t web_assets[WEB_ASSET_COUNT];

// Resource lookup functions
const web_asset_t* web_asset_find(const char* path);
const web_asset_t* web_asset_find_by_hash(uint32_t hash);
int web_asset_get_index(const char* path);

// Resource statistics functions
void web_asset_print_stats(void);
size_t web_asset_get_total_size(void);
size_t web_asset_get_compressed_size(void);
float web_asset_get_compression_ratio(void);

// Resource verification functions
bool web_asset_verify_integrity(void);
uint32_t web_asset_calculate_hash(const uint8_t* data, size_t size);

#ifdef __cplusplus
}
#endif

#endif // WEB_ASSETS_H
`;
  }

  /**
   * Generate source file
   */
  async generateSourceFile() {
    let source = `/**
 * @file web_assets.c
 * @brief Web frontend resource implementation
 * @auto-generated by static_to_c_converter.js
 * @date ${new Date().toISOString()}
 */

#include "web_assets.h"
#include <string.h>
#include <stdio.h>

// Resource file data
`;

    // Generate data array for each resource
    for (let i = 0; i < this.assets.length; i++) {
      const asset = this.assets[i];
      const varName = `asset_data_${i}`;
      const hexData = this.bufferToHex(asset.content);
      
      source += `// ${asset.path} (${asset.size} bytes${asset.isCompressed ? ', compressed' : ''})\n`;
      source += `static const uint8_t ${varName}[] = {\n`;
      source += `    ${hexData}\n`;
      source += `};\n\n`;
    }

    // Generate resource array
    source += `// Resource file array\n`;
    source += `const web_asset_t web_assets[WEB_ASSET_COUNT] = {\n`;
    
    for (let i = 0; i < this.assets.length; i++) {
      const asset = this.assets[i];
      source += `    {\n`;
      source += `        .path = "${asset.path}",\n`;
      source += `        .data = asset_data_${i},\n`;
      source += `        .size = ${asset.size},\n`;
      source += `        .mime_type = "${asset.mimeType}",\n`;
      source += `        .hash = 0x${asset.hash},\n`;
      source += `        .is_compressed = ${asset.isCompressed ? 'true' : 'false'},\n`;
      source += `        .compression_ratio = ${asset.compressionRatio.toFixed(3)}f\n`;
      source += `    },\n`;
    }
    
    source += `};\n\n`;

    // Generate lookup functions
    source += this.generateLookupFunctions();
    
    // Generate statistics functions
    source += this.generateStatsFunctions();
    
    // Generate verification functions
    source += this.generateVerificationFunctions();
    
    return source;
  }

  /**
   * Generate lookup functions
   */
  generateLookupFunctions() {
    return `
// Resource lookup function implementation
const web_asset_t* web_asset_find(const char* path) {
    if (!path) return NULL;
    
    for (int i = 0; i < WEB_ASSET_COUNT; i++) {
        if (strcmp(web_assets[i].path, path) == 0) {
            return &web_assets[i];
        }
    }
    return NULL;
}

const web_asset_t* web_asset_find_by_hash(uint32_t hash) {
    for (int i = 0; i < WEB_ASSET_COUNT; i++) {
        if (web_assets[i].hash == hash) {
            return &web_assets[i];
        }
    }
    return NULL;
}

int web_asset_get_index(const char* path) {
    if (!path) return -1;
    
    for (int i = 0; i < WEB_ASSET_COUNT; i++) {
        if (strcmp(web_assets[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}
`;
  }

  /**
   * Generate statistics functions
   */
  generateStatsFunctions() {
    return `
// Resource statistics function implementation
void web_asset_print_stats(void) {
    printf("=== Web Resource Statistics ===\\n");
    printf("File count: %d\\n", WEB_ASSET_COUNT);
    printf("Total size: %d bytes (%.2f KB)\\n", WEB_TOTAL_SIZE, WEB_TOTAL_SIZE / 1024.0f);
    printf("Compressed size: %d bytes (%.2f KB)\\n", WEB_COMPRESSED_SIZE, WEB_COMPRESSED_SIZE / 1024.0f);
    printf("Compression ratio: %.1f%%\\n", WEB_COMPRESSION_RATIO * 100.0f);
    printf("\\nFile list:\\n");
    
    for (int i = 0; i < WEB_ASSET_COUNT; i++) {
        printf("  %s: %d bytes", web_assets[i].path, web_assets[i].size);
        if (web_assets[i].is_compressed) {
            printf(" (compressed, ratio: %.1f%%)", web_assets[i].compression_ratio * 100.0f);
        }
        printf("\\n");
    }
    printf("==================\\n");
}

size_t web_asset_get_total_size(void) {
    return WEB_TOTAL_SIZE;
}

size_t web_asset_get_compressed_size(void) {
    return WEB_COMPRESSED_SIZE;
}

float web_asset_get_compression_ratio(void) {
    return WEB_COMPRESSION_RATIO;
}
`;
  }

  /**
   * Generate verification functions
   */
  generateVerificationFunctions() {
    return `
// Resource verification function implementation
uint32_t web_asset_calculate_hash(const uint8_t* data, size_t size) {
    uint32_t hash = 0;
    for (size_t i = 0; i < size; i++) {
        hash = ((hash << 5) + hash) + data[i]; // Simple hash algorithm
    }
    return hash;
}

bool web_asset_verify_integrity(void) {
    for (int i = 0; i < WEB_ASSET_COUNT; i++) {
        uint32_t calculated_hash = web_asset_calculate_hash(web_assets[i].data, web_assets[i].size);
        if (calculated_hash != web_assets[i].hash) {
            printf("ERROR: Hash mismatch for %s\\n", web_assets[i].path);
            return false;
        }
    }
    return true;
}
`;
  }

  /**
   * Generate resource index
   */
  generateResourceIndex() {
    console.log('📋 Generating resource index...');
    
    const index = {
      version: '1.0.0',
      buildTime: new Date().toISOString(),
      converter: 'static_to_c_converter.js',
      config: this.config,
      stats: this.stats,
      assets: this.assets.map(asset => ({
        path: asset.path,
        originalSize: asset.originalSize,
        size: asset.size,
        mimeType: asset.mimeType,
        hash: asset.hash,
        isCompressed: asset.isCompressed,
        compressionRatio: asset.compressionRatio
      }))
    };
    
    fs.writeFileSync(
      path.join(this.config.outputDir, 'resource-index.json'),
      JSON.stringify(index, null, 2)
    );
  }

  /**
   * Generate firmware config
   */
  generateFirmwareConfig() {
    console.log('⚙️ Generating firmware config...');
    
    const config = {
      web_assets: {
        enabled: true,
        compression: this.config.enableCompression,
        max_file_size: this.config.maxFileSize,
        total_files: this.assets.length,
        total_size: this.stats.totalSize,
        compressed_size: this.stats.compressedSize
      },
      http_server: {
        static_files_enabled: true,
        default_index: 'index.html',
        fallback_to_index: true,
        cache_control: 'max-age=31536000'
      },
      build: {
        converter_version: '1.0.0',
        build_time: new Date().toISOString(),
        input_directory: this.config.inputDir,
        output_directory: this.config.outputDir
      }
    };
    
    fs.writeFileSync(
      path.join(this.config.outputDir, 'firmware-config.json'),
      JSON.stringify(config, null, 2)
    );
  }

  /**
   * Generate binary file
   */
  async generateBinaryFile() {
    console.log('📦 Generating binary file...');
    
    const outputPath = path.join(this.config.outputDir, 'web-assets.bin');
    const writeStream = fs.createWriteStream(outputPath);
    
    // Write file header
    const header = Buffer.alloc(64);
    header.write('WEBASSETS', 0, 8, 'ascii');
    header.writeUInt32LE(0x0100, 8); // Version 1.0
    header.writeUInt32LE(this.assets.length, 12); // File count
    header.writeUInt32LE(this.stats.totalSize, 16); // Total size
    header.writeUInt32LE(this.stats.compressedSize, 20); // Compressed size
    header.writeUInt32LE(Math.floor(Date.now() / 1000), 24); // Timestamp
    
    writeStream.write(header);
    
    // Write file index
    let offset = 64 + this.assets.length * 64; // Header + index table
    
    for (const asset of this.assets) {
      const indexEntry = Buffer.alloc(64);
      // Ensure path does not exceed 23 bytes (leave 1 byte for null terminator)
      const shortPath = asset.path.length > 55 ? asset.path.substring(0, 55) : asset.path;
      indexEntry.write(shortPath, 0, 56, 'ascii'); // Path (max 24 bytes)
      indexEntry.writeUInt32LE(offset, 56); // Data offset
      indexEntry.writeUInt32LE(asset.size, 60); // Data size
      
      writeStream.write(indexEntry);
      offset += asset.size;
    }
    
    // Write file data
    for (const asset of this.assets) {	
      console.log(`[STATIC] Writing asset: ${asset.path} (${asset.size} bytes)`);
      writeStream.write(asset.content);
    }
    
    writeStream.end();
    
    await new Promise((resolve, reject) => {
      writeStream.on('finish', resolve);
      writeStream.on('error', reject);
    });
    
    console.log(`✅ Binary file generation completed: ${outputPath}`);
  }

  /**
   * Convert buffer to hexadecimal string
   */
  bufferToHex(buffer) {
    const hex = buffer.toString('hex');
    const chunks = [];
    
    for (let i = 0; i < hex.length; i += this.config.chunkSize * 2) {
      const chunk = hex.substr(i, this.config.chunkSize * 2);
      const bytes = [];
      
      for (let j = 0; j < chunk.length; j += 2) {
        bytes.push(`0x${chunk.substr(j, 2)}`);
      }
      
      chunks.push('    ' + bytes.join(', '));
    }
    
    return chunks.join(',\n');
  }

  /**
   * Calculate file hash
   */
  calculateHash(buffer) {
    return crypto.createHash('md5').update(buffer).digest('hex').substr(0, 8);
  }

  /**
   * Get MIME type
   */
  getMimeType(filename) {
    const ext = path.extname(filename).toLowerCase();
    const mimeTypes = {
      '.html': 'text/html',
      '.css': 'text/css',
      '.js': 'application/javascript',
      '.json': 'application/json',
      '.png': 'image/png',
      '.jpg': 'image/jpeg',
      '.jpeg': 'image/jpeg',
      '.gif': 'image/gif',
      '.svg': 'image/svg+xml',
      '.ico': 'image/x-icon',
      '.woff': 'font/woff',
      '.woff2': 'font/woff2',
      '.ttf': 'font/ttf',
      '.eot': 'application/vnd.ms-fontobject',
      '.webp': 'image/webp',
      '.mp4': 'video/mp4',
      '.webm': 'video/webm',
      '.mp3': 'audio/mpeg',
      '.wav': 'audio/wav',
      '.pdf': 'application/pdf',
      '.xml': 'application/xml',
      '.txt': 'text/plain'
    };
    
    return mimeTypes[ext] || 'application/octet-stream';
  }

  /**
   * Output statistics
   */
  printStats() {
    console.log('\n📊 Conversion statistics:');
    console.log(`   File count: ${this.stats.totalFiles}`);
    console.log(`   Original size: ${(this.stats.totalSize / 1024 / 1024).toFixed(2)} MB`);
    console.log(`   Compressed size: ${(this.stats.compressedSize / 1024 / 1024).toFixed(2)} MB`);
    console.log(`   Compression ratio: ${((1 - this.stats.compressedSize / this.stats.totalSize) * 100).toFixed(1)}%`);
    console.log(`   Conversion time: ${this.stats.conversionTime}ms`);
    console.log(`   Output directory: ${this.config.outputDir}`);
  }
}

// Command line interface
if (require.main === module) {
  const args = process.argv.slice(2);
  const options = {};
  
  // Parse command line arguments
  for (let i = 0; i < args.length; i++) {
    switch (args[i]) {
      case '--input':
      case '-i':
        options.inputDir = args[++i];
        break;
      case '--output':
      case '-o':
        options.outputDir = args[++i];
        break;
      case '--max-size':
        options.maxFileSize = parseInt(args[++i]);
        break;
      case '--no-compression':
        options.enableCompression = false;
        break;
      case '--help':
      case '-h':
        console.log(`
Static file to C array converter

Usage: node static_to_c_converter.js [options]

Options:
  -i, --input <dir>        Input directory (default: ./dist)
  -o, --output <dir>       Output directory (default: ./firmware_assets)
  --max-size <bytes>       Maximum file size (default: 1048576)
  --no-compression         Disable compression
  -h, --help               Show help information

Examples:
  node static_to_c_converter.js -i ./dist -o ./assets
  node static_to_c_converter.js --max-size 2097152 --no-compression
`);
        process.exit(0);
    }
  }
  
  // Execute conversion
  const converter = new StaticToCConverter(options);
  converter.convert().catch(error => {
    console.error('Conversion failed:', error);
    process.exit(1);
  });
}

module.exports = StaticToCConverter; 