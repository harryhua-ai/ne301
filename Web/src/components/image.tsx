import { useState } from 'preact/hooks';
import { Dialog, DialogContent, DialogClose } from '@/components/dialog';
import { Button } from '@/components/ui/button';

export default function Image({
  src,
  alt,
  className,
}: {
  src: string;
  alt: string;
  className?: string;
}) {
  const [isPreviewOpen, setIsPreviewOpen] = useState(false);

  const handleImageClick = () => {
    setIsPreviewOpen(true);
  };

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Enter' || e.key === ' ') {
      e.preventDefault();
      setIsPreviewOpen(true);
    }
  };

  return (
    <>
      <div
        className={`cursor-pointer transition-opacity hover:opacity-80 ${className || ''}`}
        onClick={handleImageClick}
        onKeyDown={handleKeyDown}
        tabIndex={0}
        role="button"
        aria-label={`preview image: ${alt}`}
      >
        <img src={src} alt={alt} className="h-full w-full object-contain" />
      </div>

      <Dialog open={isPreviewOpen} onOpenChange={setIsPreviewOpen}>
        <DialogContent
          className="max-h-[90vh] max-w-4xl border-0 bg-transparent p-4 shadow-none"
          showCloseButton={false}
        >
          <div className="flex items-center justify-center">
            <img
              src={src}
              alt={alt}
              className="max-h-[80vh] max-w-full object-contain"
            />
          </div>
        </DialogContent>
        {isPreviewOpen && (
          <DialogClose asChild>
            <Button
              variant="ghost"
              className="fixed top-4 right-4 z-[60] h-8 w-8 rounded-full bg-black/50 p-0 text-white transition-all hover:bg-black/70"
            >
              <svg
                xmlns="http://www.w3.org/2000/svg"
                height="24px"
                viewBox="0 -960 960 960"
                width="24px"
                fill="currentColor"
                className="h-4 w-4"
              >
                <path d="m256-200-56-56 224-224-224-224 56-56 224 224 224-224 56 56-224 224 224 224-56 56-224-224-224 224Z" />
              </svg>
            </Button>
          </DialogClose>
        )}
      </Dialog>
    </>
  );
}
