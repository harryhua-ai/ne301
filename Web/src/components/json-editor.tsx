import { useState } from 'preact/hooks'
import { Button } from '@/components/ui/button'
import { ScrollArea, ScrollBar } from '@/components/ui/scroll-area'
import SvgIcon from '@/components/svg-icon'
import { useLingui } from '@lingui/react';
import Loading from './loading'

interface JsonEditorProps {
  className?: string,
  jsonValue: string | object,
  loading?: boolean
}

  const renderJson = (jsonValue: string) => {
    const jsonString = JSON.stringify(jsonValue, null, 2);
    const lines = jsonString.split('\n');
    const digits = String(lines.length).length; // Line number column width
  
    return (
        <ScrollArea className="h-full w-full my-2 bg-gray-100 rounded-md ">
        <div className="flex p-2 items-start">
          {/* Line numbers */}
          <div
            className="mr-3 text-right select-none text-xs text-text-secondary font-mono leading-6 tabular-nums"
            style={{ width: `${digits + 1}ch` }}
          >
            {lines.map((_, i) => (
              <div key={i}>{i + 1}</div>
            ))}
          </div>
    
          {/* JSON content (rendered line by line, monospace font, fixed line height, no wrap) */}
          <div className="font-mono text-xs leading-6 whitespace-pre">
            {lines.map((line, i) => (
              <div key={i}>{line}</div>
            ))}
          </div>
        </div>
        <ScrollBar orientation="horizontal" />
        </ScrollArea>
      );
    };

export default function JsonEditor(props: JsonEditorProps) {
  const { i18n } = useLingui();
  const { className, jsonValue, loading } = props

  const [copied, setCopied] = useState(false)
  const handleCopyJson = async () => {
    const text = typeof jsonValue === 'string' ? jsonValue : JSON.stringify(jsonValue, null, 2)
    try {
      await navigator.clipboard.writeText(text)
    } catch {
      const ta = document.createElement('textarea')
      ta.value = text
      ta.style.position = 'fixed'
      ta.style.opacity = '0'
      document.body.appendChild(ta)
      ta.select()
      document.execCommand('copy')
      document.body.removeChild(ta)
    }
    setCopied(true)
    setTimeout(() => setCopied(false), 1500)
  }

  return (
    <div className={className}>
      <div className="w-full h-full pb-4">
        <div className="w-full flex items-center justify-between">
          <p className="text-xs text-text-primary">JSON</p>
          {/* eslint-disable-next-line jsx-a11y/click-events-have-key-events */}
          <Button
            variant="ghost"
            className="w-4 h-4 cursor-pointer relative"
            onClick={handleCopyJson}
            title={copied ? i18n._('common.copied') : i18n._('common.copy_to_clipboard')}
          >
            <SvgIcon icon="content_copy" />
            {copied && (
              <div className="absolute -top-8 left-1/2 transform -translate-x-1/2 bg-green-500 text-white text-xs px-2 py-1 rounded whitespace-nowrap">
                {i18n._('common.copied')}
              </div>
            )}
          </Button>
        </div>
        {loading ? (
          <div className="w-full h-full flex flex-col items-center justify-center absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2">
            <Loading placeholder={i18n._('sys.model_verification.loading')} />
          </div>
        ) : jsonValue ? (
          <div className="w-full h-full">
            {renderJson(jsonValue as string)}
          </div>
        ) : (
          <div className="w-full !h-full">
           <div className="w-full !h-full flex flex-col items-center justify-center">
           <SvgIcon className="w-40 h-40" icon="empty" />
            <p className="text-sm text-text-secondary">{i18n._('sys.model_verification.upload_local_image_desc')}</p>
            <p className="text-sm mt-2 text-text-secondary">{i18n._('sys.model_verification.json_result_desc')}</p>
           </div>
          </div>
        )}
      </div>
    </div>
  )
}