"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.bundleSource = bundleSource;
const linguiEsbuildPlugin_1 = require("./linguiEsbuildPlugin");
function createExtRegExp(extensions) {
    return new RegExp("\\.(?:" + extensions.join("|") + ")(?:\\?.*)?$");
}
async function bundleSource(linguiConfig, entryPoints, outDir, rootDir) {
    const esbuild = await import("esbuild");
    const config = linguiConfig.experimental.extractor;
    const excludeExtensions = config.excludeExtensions || [
        "ico",
        "pot",
        "xliff",
        "woff2",
        "woff",
        "eot",
        "gif",
        "otf",
        "ttf",
        "mp4",
        "svg",
        "png",
        "css",
        "sass",
        "scss",
        "less",
        "jpg",
    ];
    const esbuildOptions = {
        entryPoints: entryPoints,
        outExtension: { ".js": ".jsx" },
        jsx: "preserve",
        bundle: true,
        platform: "node",
        target: ["esnext"],
        format: "esm",
        splitting: false,
        treeShaking: true,
        outdir: outDir,
        sourcemap: "inline",
        sourceRoot: outDir,
        sourcesContent: false,
        packages: "external",
        metafile: true,
        plugins: [
            (0, linguiEsbuildPlugin_1.pluginLinguiMacro)({ linguiConfig }),
            {
                name: "externalize-files",
                setup(build) {
                    build.onResolve({ filter: createExtRegExp(excludeExtensions) }, () => ({
                        external: true,
                    }));
                },
            },
        ],
    };
    return await esbuild.build(config.resolveEsbuildOptions
        ? config.resolveEsbuildOptions(esbuildOptions)
        : esbuildOptions);
}
