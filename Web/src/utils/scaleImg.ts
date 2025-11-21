/**
 * 
 * @TODO
 * When width > height and width > 720, scale down proportionally to width 720
 * When height > width and height > 720, scale down proportionally to height 720
 * param: file: File
 * return: processed file
 */
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

const proportionScaleImg = (props: ScaleImgProps): Promise<ScaleResult> => {
    const { file, maxSize = 720, quality = 1 } = props;
    
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
            const { width, height } = img;
            
            // Calculate scaling ratio
            let newWidth = width;
            let newHeight = height;
            
            if (width >= height) {
                // Landscape or square image
                if (width > maxSize) {
                    newWidth = maxSize;
                    newHeight = height * (maxSize / width);
                }
            } else if (height > maxSize) {
                // Portrait image
                newHeight = maxSize;
                newWidth = width * (maxSize / height);
            }
            
            // If no scaling needed, return original file directly
            if (newWidth === width && newHeight === height) {
                resolve({
                    width,
                    height,
                    scaledFile: file
                });
                return;
            }
            
            // Set canvas size
            canvas.width = newWidth;
            canvas.height = newHeight;
            
            // Draw scaled image
            ctx.drawImage(img, 0, 0, newWidth, newHeight);
            
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
                        width: newWidth,
                        height: newHeight,
                        scaledFile
                    });
                },
                file.type,
                quality
            );
            
            // Clean up resources
            URL.revokeObjectURL(img.src);
        };
        
        img.onerror = () => {
            URL.revokeObjectURL(img.src);
            reject(new Error('Failed to load image'));
        };
        
        img.src = URL.createObjectURL(file);
    });
};

/**
 * 
 * @TODO
 * When width > or < aiImgWidth, scale proportionally to aiImgWidth
 * When height > or < aiImgHeight, scale proportionally to aiImgHeight
 * Can stretch/scale image, cannot crop image
 * param: file: File
 * aiImgHeight: number
 * aiImgWidth: number
 * return: processed file
 * @returns 
 */
interface FixedScaleImgProps {
    file: File;
    aiImgHeight: number;
    aiImgWidth: number;
    quality?: number;
}
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
                quality // Fixed quality at 1
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

export { fixedScaleImg, proportionScaleImg };
