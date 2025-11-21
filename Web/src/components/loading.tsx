import { createPortal } from 'preact/compat';

export default function Loading({ placeholder, isMask = false }: { placeholder?: string, isMask?: boolean }) {
    const content = (
        <div className="w-full h-full min-h-[120px] flex flex-col items-center justify-center overflow-hidden">
            <div className="w-8 h-8 mb-2 rounded-full border-4 border-[#f24a00] border-t-transparent animate-spin" aria-label="loading" />
            <span className="text-sm mt-2 text-primary min-h-[20px] leading-none">
                {placeholder ?? ''}
            </span>
        </div>
    )

    if (isMask && typeof document !== 'undefined') {
        return createPortal(
            <div className="fixed inset-0 bg-black/50 z-[100000] pointer-events-auto">
                {content}
            </div>,
            document.body
        )
    }
    return content
}