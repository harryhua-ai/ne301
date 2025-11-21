import { useEffect, useState, useRef } from 'preact/compat';
import type { ComponentChildren } from 'preact';
import { useDropzone } from '@/lib/upload';
import { Button } from '@/components/ui/button';
import Loading from '@/components/loading';
import { useLingui } from '@lingui/react';
import SvgIcon from '@/components/svg-icon';
import type { AcceptProp } from '@/types/upload';
import { toast } from 'sonner';

// Unified export type alias for external use
export type AcceptType =
  | string
  | readonly string[]
  | Record<string, readonly string[]>;

interface UploadProps {
  className?: string;
  type: string;
  fileType?: string | string[];
  accept?: AcceptType;
  maxFiles?: number;
  maxSize?: number;
  minSize?: number;
  multiple?: boolean;
  // Whether to allow folder selection
  directory?: boolean;
  loading?: boolean;
  onFileChange?: (file: File) => void;
  onFilesChange?: (files: File[]) => void;
  onReject?: (rejections: { errors: any; file: File }[]) => void;
  slot?: ComponentChildren | (() => ComponentChildren);
  disabled?: boolean;
  btnVariant?: string;
}
export default function Upload(props: UploadProps) {
  const { i18n } = useLingui();
  const [file, setFile] = useState<File | null>(null);
  const [files, setFiles] = useState<File[]>([]);
  const uploadRef = useRef<HTMLDivElement>(null);
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  const {
    className,
    type = 'button',
    btnVariant = 'outline',
    accept = {},
    loading = false,
    onFileChange,
    onFilesChange,
    onReject,
    slot,
    maxSize = 1024,
    disabled = false,
    directory = false,
    multiple = false,
  } = props;
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  const { getRootProps, getInputProps, acceptedFiles, fileRejections } = useDropzone({
    onDragEnter: () => {
      console.log('onDragEnter');
      if (uploadRef.current) {
        uploadRef.current.classList.remove('bg-gray-100');
        uploadRef.current.classList.add('bg-gray-50', 'border-gray-200');
      }
    },
    onDragLeave: () => {
      console.log('onDragLeave');
      if (uploadRef.current) {
        uploadRef.current.classList.remove('bg-gray-200', 'border-gray-400');
        uploadRef.current.classList.add('bg-gray-100');
      }
    },
    onDrop: () => {
      // console.log('drop')
    },
    onError: (error: any) => {
      toast.error(error.message);
    },
    onDropRejected: (rejections: any) => {
      toast.error(rejections[0].errors[0].message);
    },
    accept: accept as AcceptProp,
    maxSize,
    multiple: multiple || false,
  });

  // Pass rejected file information to parent component
  useEffect(() => {
    if (onReject && fileRejections?.length) {
      // Normalize to {errors, file}[] structure
      const normalized = fileRejections.map((r: any) => ({
        errors: r.errors,
        file: r.file as File,
      }));
      onReject(normalized);
    }
  }, [fileRejections, onReject]);

  const filesCallback = () => {
    if (!acceptedFiles?.length) return;
    acceptedFiles.forEach((f: File) => {
      if (onFileChange) {
        onFileChange(f);
        setFile(f);
      }
    });
    setFiles(prev => {
      const map = new Map<string, File>();
      prev.forEach(pf => map.set(`${pf.name}-${pf.size}`, pf));
      acceptedFiles.forEach(nf => map.set(`${nf.name}-${nf.size}`, nf));
      const merged = Array.from(map.values());
      if (onFilesChange) onFilesChange(merged);
      return merged;
    });
  };

  useEffect(() => {
    filesCallback();
  }, [acceptedFiles]);
  // type === button
  const uploadButton = () => (
    <Button
      {...getRootProps()}
      variant={btnVariant}
      className="w-full h-full"
      disabled={disabled || loading}
    >
      {loading ? (
        <div className="w-full h-full flex items-center justify-center">
          <div className="w-4 h-4 rounded-full border-2 border-[#f24a00] border-t-transparent animate-spin" aria-label="loading" />

        </div>
      ) : typeof slot === 'function' ? (slot as () => ComponentChildren)() : slot}
      {/* Allow folder selection: inject webkitdirectory/directory attribute to input */}
      {/* eslint-disable-next-line */}
      {/* @ts-ignore */}
      <input
        {...getInputProps()}
        {...(directory ? { webkitdirectory: true, directory: true } : {})}
      />
    </Button>
  );

  const acceptText = Array.isArray(accept)
    ? (accept as readonly string[]).join(',').replace(/[^,]*\//g, '')
    : typeof accept === 'string'
      ? accept.replace(/[^,]*\//g, '')
      : accept
        ? Object.keys(accept)
          .join(',')
          .replace(/[^,]*\//g, '')
        : '';

  const uploadZoneSingle = () => (
    <div
      {...getRootProps()}
      className="flex flex-col items-center justify-center "
    >
      {/* eslint-disable-next-line */}
      {/* @ts-ignore */}
      <input
        {...getInputProps()}
        {...(directory ? { webkitdirectory: true, directory: true } : {})}
      />
      {acceptedFiles.length > 0 ? (
        <SvgIcon icon="uploaded_single" />
      ) : (
        <SvgIcon icon="upload_single" />
      )}
      {i18n._('common.upload')}
      <Button className="my-4" variant="outline" size="icon">
        <SvgIcon className="w-20 h-20" icon="upload_single" />
      </Button>
      {/* eslint-disable-next-line */}
      {/* @ts-ignore */}
      <p>
        {i18n._('common.upload_single')}
        <span>{acceptText}</span>
      </p>
      {file
        && slot
        && (typeof slot === 'function'
          ? (slot as () => ComponentChildren)()
          : slot)}
    </div>
  );

  const uploadImgZone = () => {
    const inputProps = getInputProps();
    const rootProps = getRootProps();
    // Extract ref from rootProps, then merge
    const { ref: dropzoneRef, ...restRootProps } = rootProps;
    
    // Merge refs to ensure both uploadRef and dropzone ref are correctly set
    const mergedRef = (node: HTMLDivElement | null) => {
      uploadRef.current = node;
      if (dropzoneRef) {
        if (typeof dropzoneRef === 'function') {
          dropzoneRef(node);
        } else if (dropzoneRef && typeof dropzoneRef === 'object') {
          (dropzoneRef as { current: HTMLDivElement | null }).current = node;
        }
      }
    };
    
    return (
      <div
        {...restRootProps}
        ref={mergedRef}
        className="w-full h-full relative flex flex-col rounded-md bg-gray-100"
      >
        {!loading ? (
          <>
            <div
              className="w-full relative flex-1 py-8 flex flex-col items-center justify-center pointer-events-none"
            >
              <div className="w-16 mb-2">
                <SvgIcon className="w-20 h-20" icon="upload_single" />
              </div>
              <p className="text-sm text-primary">
                {i18n._('common.upload_single')}
              </p>
              {acceptText && (
                <p className="text-xs mt-2 text-text-secondary">{acceptText}</p>
              )}
            </div>
            <div className="w-full flex gap-2 items-center justify-center my-2 pointer-events-none">
              {file
                && slot
                && (typeof slot === 'function'
                  ? (slot as () => ComponentChildren)()
                  : slot)}
            </div>
          </>
        ) : (
          <Loading />
        )}
        {/* Place input last to ensure it's on top layer for clicking and dragging */}
        <input
          {...inputProps}
          {...(directory ? { webkitdirectory: true, directory: true } : {})}
          className="!w-full !h-full absolute top-0 left-0 opacity-0 z-[9999] cursor-pointer pointer-events-auto"
        />
      </div>
    );
  };

  const multipleUploadZone = () => (
    <div
      ref={uploadRef}
      {...getRootProps()}
      className="w-full h-full border-1 border-dashed border-gray-300 rounded-md bg-gray-100 py-8"
    >
      {loading ? (
        <Loading />
      ) : (
        <div  className="w-full h-full">
          {(files.length > 0 || acceptedFiles.length > 0) && (
            <div className="h-full flex flex-col items-center gap-2">
              <div className="flex grid-flow-row gap-2">
                {(files.length > 0 ? files : acceptedFiles).map((f: File) => (
                  <div
                    className="flex flex-col items-center gap-2"
                    key={f.name}
                  >
                    <div className="w-10 h-10 bg-gray-500 p-2 rounded-md flex items-center justify-center">
                      <SvgIcon icon="file" />
                    </div>
                    <p className="text-sm text-text-primary">{f.name}</p>
                  </div>
                ))}
              </div>
              <Button className="my-4 justify-self-end" variant="outline">
                {i18n._('sys.header.select_file')}
              </Button>
            </div>
          )}
          {files.length === 0 && acceptedFiles.length === 0 && (
            <div className="h-full flex flex-col items-center gap-2">
              <div>
                <SvgIcon className="w-20 h-20" icon="upload_single" />
              </div>
              <Button className="my-4 justify-self-end" variant="outline">
                {i18n._('sys.header.select_file')}
              </Button>
              <p className="text-sm text-text-secondary">
                {i18n._('common.upload_single')}
              </p>
            </div>
          )}

          <input
            {...getInputProps()}
            {...(directory ? { webkitdirectory: true, directory: true } : {})}
          />
        </div>
      )}
    </div>
  );

  const customUploadZone = () => {
    const inputProps = getInputProps();
    const rootProps = getRootProps();
    // Extract ref from rootProps, then merge
    const { ref: dropzoneRef, ...restRootProps } = rootProps;
    
    // Merge refs to ensure both uploadRef and dropzone ref are correctly set
    const mergedRef = (node: HTMLDivElement | null) => {
      uploadRef.current = node;
      if (dropzoneRef) {
        if (typeof dropzoneRef === 'function') {
          dropzoneRef(node);
        } else if (dropzoneRef && typeof dropzoneRef === 'object') {
          (dropzoneRef as { current: HTMLDivElement | null }).current = node;
        }
      }
    };
    
    return (
      <div {...restRootProps} ref={mergedRef} className="w-full h-full relative flex flex-col rounded-md border-1 border-dashed border-gray-300 bg-gray-100">
        {loading ? <Loading placeholder={i18n._('common.uploading')} /> : (
          <>
            {typeof slot === 'function' ? (slot as () => ComponentChildren)() : slot}
            {/* <input
              {...inputProps}
              {...(directory ? { webkitdirectory: true, directory: true } : {})}
              className="!w-full !h-full absolute top-0 left-0 opacity-0 z-[9999] cursor-pointer pointer-events-auto"
            /> */}
                <input
                  {...inputProps}
                  {...(directory ? { webkitdirectory: true, directory: true } : {})}
                  className="!w-full !h-full absolute top-0 left-0 opacity-0 z-[9999] cursor-pointer pointer-events-auto"
                />
          </>
        )}
      </div>
    );
  };

  return (
    <div className={className}>
      {type === 'button' && uploadButton()}
      {type === 'zoneSingle' && (
        <div className="w-full h-full border-1 border-dashed border-gray-300 rounded-md bg-gray-100 py-8">
          {uploadZoneSingle()}
        </div>
      )}
      {type === 'imgZone' && uploadImgZone()}
      {type === 'multipleZone' && multipleUploadZone()}
      {type === 'customZone' && customUploadZone()}
    </div>
  );
}
