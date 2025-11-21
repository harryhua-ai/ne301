interface DetectionBox {
    class: string;
    confidence: number;
    bbox: {
        x: number;
        y: number;
        width: number;
        height: number;
    };
}

interface RenderOptions {
    strokeColor?: string;
    lineWidth?: number;
    fontSize?: number;
    showConfidence?: boolean;
    showClass?: boolean;
}

interface ScaleImgProps {
    file: File;
    maxSize?: number;
    quality?: number;
}

interface ScaleResult {
    width: number;
    height: number;
    scaledFile: File;
}

interface FixedScaleImgProps {
    file: File;
    aiImgHeight: number;
    aiImgWidth: number;
    quality?: number;
}

// Point annotation rendering
function renderCanvasImg(
    imageSrc: string,
    detections: DetectionBox[],
    options: RenderOptions = {}
): Promise<HTMLCanvasElement> {
    return new Promise((resolve, reject) => {
        const { strokeColor = '#F00', lineWidth = 2, fontSize = 14, showConfidence = true, showClass = true } = options;
        const canvas = document.createElement('canvas');
        const ctx = canvas.getContext('2d');
        if (!ctx) {
            reject(new Error('no ctx'));
            return;
        }
        const imgEl = new Image();
        imgEl.crossOrigin = 'anonymous';
        imgEl.onload = () => {
            canvas.width = imgEl.naturalWidth;
            canvas.height = imgEl.naturalHeight;
            ctx.drawImage(imgEl, 0, 0);

            for (const d of detections) {
                const { x, y, width, height } = d.bbox;
                if (!x || !y || !width || !height) {
                    continue;
                }
                ctx.strokeStyle = strokeColor;
                ctx.lineWidth = lineWidth;
                ctx.strokeRect(x, y, width, height);
                const label = `${showClass ? d.class : ''}${showConfidence ? ` ${(d.confidence * 100).toFixed(1)}%` : ''}`.trim();
                if (label) {
                    ctx.font = `${fontSize}px Arial`;
                    const w = ctx.measureText(label).width + 8; const
                        h = fontSize + 4;
                    ctx.fillStyle = strokeColor; ctx.fillRect(x, y - h, w, h);
                    ctx.fillStyle = '#fff'; ctx.fillText(label, x + 4, y - 4);
                }
            }
            resolve(canvas);
        };
        imgEl.onerror = () => reject(new Error('image load error'));
        imgEl.src = imageSrc;
    });
}

// // Convert canvas to image URL
function canvasToImageUrl(canvas: HTMLCanvasElement): string {
    return canvas.toDataURL('image/png');
}

/**
 * 
 * @TODO
 * When width is greater than height, if width > 720, proportionally scale down to width 720
 * When height is greater than width, if height > 720, proportionally scale down to height 720 - multiple of 16
 * param: file: File
 * return: processed file
 */

const proportionScaleImg = (props: ScaleImgProps): Promise<ScaleResult> => {
    const { file, maxSize = 720, quality = 1 } = props;

    return new Promise((resolve, reject) => {
        if (!file || !file.type.startsWith('image/')) {
            reject(new Error('File is not an image'));
            return;
        }

        const img = new Image();
        const url = URL.createObjectURL(file);
        const canvas = document.createElement('canvas');
        const ctx = canvas.getContext('2d');

        if (!ctx) {
            URL.revokeObjectURL(url);
            reject(new Error('Failed to get canvas context'));
            return;
        }

        img.onload = () => {
            try {
                const origW = img.naturalWidth || img.width;
                const origH = img.naturalHeight || img.height;

                if (!origW || !origH) {
                    URL.revokeObjectURL(url);
                    reject(new Error('Cannot read image natural dimensions'));
                    return;
                }

                let targetW = origW;
                let targetH = origH;

                if (origW > origH) {
                    targetW = origW >= maxSize ? maxSize : origW
                    targetW = targetW - (targetW % 16) || 16
                
                    const scaleH = Math.floor(origH * (targetW / origW))
                    targetH = scaleH - (scaleH % 16) || 16
                  } else {
                    targetH = origH >= maxSize ? maxSize : origH
                    targetH = targetH - (targetH % 16) || 16
                
                    const scaleW = Math.floor(origW * (targetH / origH))
                    targetW = scaleW - (scaleW % 16) || 16
                  }
                // Set canvas and draw
                canvas.width = targetW;
                canvas.height = targetH;
                ctx.drawImage(img, 0, 0, targetW, targetH);

                canvas.toBlob(
                    (blob) => {
                        URL.revokeObjectURL(url);

                        if (!blob) {
                            reject(new Error('Failed to create blob'));
                            return;
                        }

                        const scaledFile = new File([blob], file.name, {
                            type: 'image/jpeg',
                            lastModified: Date.now()
                        });

                        resolve({
                            width: targetW,
                            height: targetH,
                            scaledFile
                        });
                    },
                    file.type,
                    quality
                );
            } catch (err) {
                URL.revokeObjectURL(url);
                reject(err);
            }
        };

        img.onerror = () => {
            URL.revokeObjectURL(url);
            reject(new Error('Failed to load image'));
        };

        img.src = url;
    });
};
/**
 * 
 * @TODO
 * When width is greater than or less than aiImgWidth, proportionally scale to aiImgWidth
 * When height is greater than or less than aiImgHeight, proportionally scale to aiImgHeight
 * Can stretch/scale image, cannot crop image
 * param: file: File
 * aiImgHeight: number
 * aiImgWidth: number
 * return: processed file
 * @returns 
 */

const fixedScaleImg = (props: FixedScaleImgProps): Promise<ScaleResult> => {
    const { file, aiImgWidth, aiImgHeight, quality = 1 } = props;

    return new Promise((resolve, reject) => {
        // Check if file is an image
        if (!file.type.startsWith('image/')) {
            reject(new Error('File is not an image'));
            return;
        }

        const img = new Image();
        const canvas = document.createElement('canvas');
        const ctx = canvas.getContext('2d');

        if (!ctx) {
            reject(new Error('Failed to get canvas context'));
            return;
        }

        img.onload = () => {
            // Set canvas to target size
            canvas.width = aiImgWidth;
            canvas.height = aiImgHeight;

            // Directly stretch image to target size, do not maintain aspect ratio
            ctx.drawImage(img, 0, 0, aiImgWidth, aiImgHeight);

            // Convert to Blob
            canvas.toBlob(
                (blob) => {
                    if (!blob) {
                        reject(new Error('Failed to create blob'));
                        return;
                    }

                    // Create new File object
                    const scaledFile = new File([blob], file.name, {
                        type: file.type,
                        lastModified: Date.now()
                    });

                    resolve({
                        width: aiImgWidth,
                        height: aiImgHeight,
                        scaledFile
                    });
                },
                file.type,
                quality
            );
            URL.revokeObjectURL(img.src);
        };

        img.onerror = () => {
            URL.revokeObjectURL(img.src);
            reject(new Error('Failed to load image'));
        };

        img.src = URL.createObjectURL(file);
    });
};

function convertToJpeg(file: File): Promise<Blob> {
    return new Promise((resolve, reject) => {
        const img = new Image();
        img.crossOrigin = "anonymous";
        img.onload = (): void => {
            const canvas = document.createElement("canvas");
            canvas.width = img.width;
            canvas.height = img.height;
            const ctx = canvas.getContext("2d");
            if (!ctx) {
                reject(new Error("Canvas not supported"));
                return;
            }

            ctx.drawImage(img, 0, 0);
            canvas.toBlob(
                (blob) => {
                    if (blob) resolve(blob);
                    else reject(new Error("Failed to convert"));
                },
                "image/jpeg",
                1   // Quality 0~1
            );
        };
        img.onerror = () => {
            reject(new Error("Image load error"));
        };
        img.src = URL.createObjectURL(file);
    });
}

export { fixedScaleImg, proportionScaleImg, renderCanvasImg, canvasToImageUrl, convertToJpeg };
