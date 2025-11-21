// Web/src/types/upload.ts
export interface allFilesAccepted {
  files: File[];
  accept: string;
  minSize: number;
  maxSize: number;
  multiple: boolean;
  maxFiles: number;
  validator: (file: File) => FileError | FileError[] | null;
}

// File rejection type
export interface FileRejection {
    file: File;
    errors: FileError[];
  }
  
  // File error type
  export interface FileError {
    code: string;
    message: string;
  }
  
  // Accepted file type configuration
  export interface AcceptProp {
    [key: string]: string[];
  }
  
  // Dropzone state
  export interface DropzoneState {
    isFocused: boolean;
    isFileDialogActive: boolean;
    isDragActive: boolean;
    isDragAccept: boolean;
    isDragReject: boolean;
    acceptedFiles: File[];
    fileRejections: FileRejection[];
  }
  
  // Dropzone methods
  export interface DropzoneMethods {
    getRootProps: (options?: GetRootPropsOptions) => any;
    getInputProps: (options?: GetInputPropsOptions) => any;
    open: () => void;
    rootRef: React.RefObject<HTMLElement>;
    inputRef: React.RefObject<HTMLInputElement>;
  }
  
  // getRootProps options
  export interface GetRootPropsOptions {
    refKey?: string;
    role?: string;
    onKeyDown?: (event: React.KeyboardEvent<HTMLDivElement>) => void;
    onFocus?: (event: React.FocusEvent<HTMLDivElement>) => void;
    onBlur?: (event: React.FocusEvent<HTMLDivElement>) => void;
    onClick?: (event: React.MouseEvent<HTMLDivElement>) => void;
    onDragEnter?: (event: React.DragEvent<HTMLDivElement>) => void;
    onDragOver?: (event: React.DragEvent<HTMLDivElement>) => void;
    onDragLeave?: (event: React.DragEvent<HTMLDivElement>) => void;
    onDrop?: (event: React.DragEvent<HTMLDivElement>) => void;
    [key: string]: any;
  }
  
  // getInputProps options
  export interface GetInputPropsOptions {
    refKey?: string;
    onChange?: (event: React.ChangeEvent<HTMLInputElement>) => void;
    onClick?: (event: React.MouseEvent<HTMLInputElement>) => void;
    [key: string]: any;
  }
  
  // Dropzone component Props
  export interface DropzoneProps {
    children: (props: DropzoneState & DropzoneMethods) => React.ReactElement;
    accept?: AcceptProp;
    disabled?: boolean;
    getFilesFromEvent?: (event: any) => File[] | Promise<File[]>;
    maxSize?: number;
    minSize?: number;
    multiple?: boolean;
    maxFiles?: number;
    preventDropOnDocument?: boolean;
    noClick?: boolean;
    noKeyboard?: boolean;
    noDrag?: boolean;
    noDragEventsBubbling?: boolean;
    validator?: (file: File) => FileError | FileError[] | null;
    useFsAccessApi?: boolean;
    autoFocus?: boolean;
    onFileDialogCancel?: () => void;
    onFileDialogOpen?: () => void;
    onDragEnter?: (event: React.DragEvent<HTMLDivElement>) => void;
    onDragLeave?: (event: React.DragEvent<HTMLDivElement>) => void;
    onDragOver?: (event: React.DragEvent<HTMLDivElement>) => void;
    onDrop?: (acceptedFiles: File[], fileRejections: FileRejection[], event: React.DragEvent<HTMLDivElement>) => void;
    onDropAccepted?: (files: File[], event: React.DragEvent<HTMLDivElement>) => void;
    onDropRejected?: (fileRejections: FileRejection[], event: React.DragEvent<HTMLDivElement>) => void;
    onError?: (error: Error) => void;
  }
  
  // useDropzone Hook return type
  export type UseDropzoneReturn = DropzoneState & DropzoneMethods;
  
  // Event callback types
  export type DragCallback = (event: React.DragEvent<HTMLDivElement>) => void;
  export type DropCallback = (acceptedFiles: File[], fileRejections: FileRejection[], event: React.DragEvent<HTMLDivElement>) => void;
  export type DropAcceptedCallback = (files: File[], event: React.DragEvent<HTMLDivElement>) => void;
  export type DropRejectedCallback = (fileRejections: FileRejection[], event: React.DragEvent<HTMLDivElement>) => void;
  export type ErrorCallback = (error: Error) => void;
  export type FileDialogCallback = () => void;
  export type GetFilesFromEvent = (event: any) => File[] | Promise<File[]>;
  export type Validator = (file: File) => FileError | FileError[] | null;