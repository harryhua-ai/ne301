/* eslint prefer-template: 0 */
import React, {
  forwardRef,
  Fragment,
  useCallback,
  useEffect,
  useImperativeHandle,
  useMemo,
  useReducer,
  useRef,
} from 'preact/compat';
import {
  acceptPropAsAcceptAttr,
  allFilesAccepted,
  composeEventHandlers,
  fileAccepted,
  fileMatchSize,
  canUseFileSystemAccessAPI,
  isAbort,
  isEvtWithFiles,
  isIeOrEdge,
  isPropagationStopped,
  isSecurityError,
  onDocumentDragOver,
  pickerOptionsFromAccept,
  getTooManyFilesRejection,
} from './tool.ts';
import type {
  DropzoneProps,
  DropzoneState,
  DropzoneMethods,
  GetRootPropsOptions,
  GetInputPropsOptions,
  FileRejection,
  FileError,
} from '@/types/upload';

// Initial state type
const initialState: DropzoneState = {
  isFocused: false,
  isFileDialogActive: false,
  isDragActive: false,
  isDragAccept: false,
  isDragReject: false,
  acceptedFiles: [],
  fileRejections: [],
};

function getFilesFromEvent(event: any): File[] | Promise<File[]> {
  // Handle File System Access API's FileSystemFileHandle
  if (Array.isArray(event) && event.length > 0 && event[0]?.kind === 'file') {
    return Promise.all(
      event.map(async (handle: any) => {
        if (handle.kind === 'file') {
          // eslint-disable-next-line no-return-await
          return await handle.getFile();
        }
        return null;
      })
    ).then(files => files.filter(Boolean));
  }

  // Handle drag events
  if (event.dataTransfer?.files) {
    return Array.from(event.dataTransfer.files);
  }

  // Handle input events
  if (event.target?.files) {
    return Array.from(event.target.files);
  }

  // Handle other possible file sources
  if (event.files) {
    return Array.from(event.files);
  }

  // If no files found, return empty array
  return [];
}
// Default properties
const defaultProps: Partial<DropzoneProps> = {
  disabled: false,
  getFilesFromEvent,
  maxSize: Infinity,
  minSize: 0,
  multiple: true,
  maxFiles: 0,
  preventDropOnDocument: true,
  noClick: false,
  noKeyboard: false,
  noDrag: false,
  noDragEventsBubbling: false,
  validator: undefined,
  useFsAccessApi: false,
  autoFocus: false,
};

/**
 * Convenience wrapper component for the `useDropzone` hook
 */
// eslint-disable-next-line
const Dropzone = forwardRef<{ open: () => void }, DropzoneProps>(
  ({ children, ...params }, ref) => {
    const { open } = useDropzone(params);

    useImperativeHandle(ref, () => ({ open }), [open]);

    return React.createElement(
      Fragment,
      null,
      children({ ...params, open } as any)
    );
  });

Dropzone.displayName = 'Dropzone';
Dropzone.defaultProps = defaultProps;

export default Dropzone;

/**
 * A React hook that creates a drag 'n' drop area.
 */
export function useDropzone(
  props: Partial<DropzoneProps> = {}
): DropzoneState & DropzoneMethods {
  const {
    accept,
    disabled,
    // getFilesFromEvent,
    maxSize,
    minSize,
    multiple,
    maxFiles,
    onDragEnter,
    onDragLeave,
    onDragOver,
    onDrop,
    onDropAccepted,
    onDropRejected,
    onFileDialogCancel,
    onFileDialogOpen,
    useFsAccessApi,
    autoFocus,
    preventDropOnDocument,
    noClick,
    noKeyboard,
    noDrag,
    noDragEventsBubbling,
    onError,
    validator,
  } = {
    ...defaultProps,
    ...props,
  };

  const acceptAttr = useMemo(() => acceptPropAsAcceptAttr(accept), [accept]);
  const pickerTypes = useMemo(() => pickerOptionsFromAccept(accept), [accept]);

  const onFileDialogOpenCb = useMemo(
    () => (typeof onFileDialogOpen === 'function' ? onFileDialogOpen : noop),
    [onFileDialogOpen]
  );
  const onFileDialogCancelCb = useMemo(
    () => (typeof onFileDialogCancel === 'function' ? onFileDialogCancel : noop),
    [onFileDialogCancel]
  );

  const rootRef = useRef<HTMLElement>(null);
  const inputRef = useRef<HTMLInputElement>(null);

  const [state, dispatch] = useReducer(reducer, initialState);
  const { isFocused, isFileDialogActive } = state;

  const fsAccessApiWorksRef = useRef(
    typeof window !== 'undefined'
      && window.isSecureContext
      && useFsAccessApi
      && canUseFileSystemAccessAPI()
  );

  // Update file dialog active state when the window is focused on
  const onWindowFocus = useCallback(() => {
    if (!fsAccessApiWorksRef.current && isFileDialogActive) {
      setTimeout(() => {
        if (inputRef.current) {
          const { files } = inputRef.current;

          if (files && !files.length) {
            dispatch({ type: 'closeDialog' });
            onFileDialogCancelCb();
          }
        }
      }, 300);
    }
  }, [isFileDialogActive, onFileDialogCancelCb]);

  useEffect(() => {
    window.addEventListener('focus', onWindowFocus, false);
    return () => {
      window.removeEventListener('focus', onWindowFocus, false);
    };
  }, [onWindowFocus]);

  const dragTargetsRef = useRef<EventTarget[]>([]);

  const onDocumentDrop = useCallback((event: DragEvent) => {
    if (
      rootRef.current
      && rootRef.current instanceof HTMLElement
      && rootRef.current.contains(event.target as Node)
    ) {
      return;
    }
    event.preventDefault();
    dragTargetsRef.current = [];
  }, []);

  useEffect(() => {
    if (preventDropOnDocument) {
      document.addEventListener(
        'dragover',
        onDocumentDragOver as EventListener,
        false
      );
      document.addEventListener('drop', onDocumentDrop as EventListener, false);
    }

    return () => {
      if (preventDropOnDocument) {
        document.removeEventListener(
          'dragover',
          onDocumentDragOver as EventListener
        );
        document.removeEventListener('drop', onDocumentDrop as EventListener);
      }
    };
  }, [preventDropOnDocument, onDocumentDrop]);

  // Auto focus the root when autoFocus is true
  useEffect(() => {
    if (!disabled && autoFocus && rootRef.current) {
      rootRef.current.focus();
    }
  }, [autoFocus, disabled]);

  const onErrCb = useCallback(
    (e: Error) => {
      if (onError) {
        onError(e);
      } else {
        // log error
      }
    },
    [onError]
  );

  const onDragEnterCb = useCallback(
    (event: React.DragEvent<HTMLElement>) => {
      event.preventDefault();
      // event.persist();
      stopPropagation(event);
      if (event.target) {
        dragTargetsRef.current = [...dragTargetsRef.current, event.target];
      }

      if (isEvtWithFiles(event as any)) {
        if (getFilesFromEvent) {
          Promise.resolve(getFilesFromEvent(event))
            .then((files: File[]) => {
              if (isPropagationStopped(event as any) && !noDragEventsBubbling) {
                return;
              }

              const fileCount = files.length;
              const isDragAccept =                fileCount > 0
                && allFilesAccepted({
                  files,
                  accept: acceptAttr || '',
                  minSize: minSize || 0,
                  maxSize: maxSize || Infinity,
                  multiple: multiple || true,
                  maxFiles: maxFiles || 0,
                  validator: validator || (() => null),
                });
              const isDragReject = fileCount > 0 && !isDragAccept;

              dispatch({
                isDragAccept,
                isDragReject,
                isDragActive: true,
                type: 'setDraggedFiles',
              });

              if (onDragEnter) {
                onDragEnter(event as any);
              }
            })
            .catch((e: Error) => onErrCb(e));
        }
      }
    },
    [
      getFilesFromEvent,
      onDragEnter,
      onErrCb,
      noDragEventsBubbling,
      acceptAttr,
      minSize,
      maxSize,
      multiple,
      maxFiles,
      validator,
    ]
  );

  const onDragOverCb = useCallback(
    (event: React.DragEvent<HTMLElement>) => {
      event.preventDefault();
      // event.persist();
      stopPropagation(event);

      const hasFiles = isEvtWithFiles(event as any);
      if (hasFiles && event.dataTransfer) {
        try {
          event.dataTransfer.dropEffect = 'copy';
        } catch {} /* eslint-disable-line no-empty */
      }

      if (hasFiles && onDragOver) {
        onDragOver(event as any);
      }

      return false;
    },
    [onDragOver, noDragEventsBubbling]
  );

  const onDragLeaveCb = useCallback(
    (event: React.DragEvent<HTMLElement>) => {
      event.preventDefault();
      // event.persist();
      stopPropagation(event);

      const targets = dragTargetsRef.current.filter(
        target => rootRef.current
          && rootRef.current instanceof HTMLElement
          && rootRef.current.contains(target as Node)
      );

      const targetIdx = targets.indexOf(event.target as EventTarget);
      if (targetIdx !== -1) {
        targets.splice(targetIdx, 1);
      }
      dragTargetsRef.current = targets;

      if (targets.length > 0) {
        return;
      }

      dispatch({
        type: 'setDraggedFiles',
        isDragActive: false,
        isDragAccept: false,
        isDragReject: false,
      });

      if (isEvtWithFiles(event as any) && onDragLeave) {
        onDragLeave(event as any);
      }
    },
    [rootRef, onDragLeave, noDragEventsBubbling]
  );

  const setFiles = useCallback(
    (files: File[], event: React.DragEvent<HTMLElement> | null) => {
      const acceptedFiles: File[] = [];
      const fileRejections: FileRejection[] = [];

      files.forEach(file => {
        const [accepted, acceptError] = fileAccepted(file, acceptAttr || '');
        const [sizeMatch, sizeError] = fileMatchSize(
          file,
          minSize || 0,
          maxSize || Infinity
        );
        const customErrors = validator ? validator(file) : null;

        if (accepted && sizeMatch && !customErrors) {
          acceptedFiles.push(file);
        } else {
          let errors: FileError[] = [
            acceptError as FileError,
            sizeError as FileError,
          ];

          if (customErrors) {
            errors = errors.concat(customErrors);
          }

          fileRejections.push({ file, errors: errors.filter(e => e) });
        }
      });

      if (
        (!multiple && acceptedFiles.length > 1)
        || (multiple
          && maxFiles
          && maxFiles >= 1
          && acceptedFiles.length > maxFiles)
      ) {
        acceptedFiles.forEach(file => {
          fileRejections.push({ file, errors: [getTooManyFilesRejection()] });
        });
        acceptedFiles.splice(0);
      }

      dispatch({
        acceptedFiles,
        fileRejections,
        isDragReject: fileRejections.length > 0,
        type: 'setFiles',
      });

      if (onDrop) {
        onDrop(acceptedFiles, fileRejections, event as any);
      }

      if (fileRejections.length > 0 && onDropRejected) {
        onDropRejected(fileRejections, event as any);
      }

      if (acceptedFiles.length > 0 && onDropAccepted) {
        onDropAccepted(acceptedFiles, event as any);
      }
    },
    [
      multiple,
      acceptAttr,
      minSize,
      maxSize,
      maxFiles,
      onDrop,
      onDropAccepted,
      onDropRejected,
      validator,
    ]
  );

  const onDropCb = useCallback(
    (event: React.DragEvent<HTMLElement>) => {
      event.preventDefault();
      // event.persist();
      stopPropagation(event);

      dragTargetsRef.current = [];

      if (isEvtWithFiles(event as any)) {
        if (getFilesFromEvent) {
          Promise.resolve(getFilesFromEvent(event))
            .then((files: File[]) => {
              if (isPropagationStopped(event as any) && !noDragEventsBubbling) {
                return;
              }
              setFiles(files, event as any);
            })
            .catch((e: Error) => onErrCb(e));
        }
      }
      dispatch({ type: 'reset' });
    },
    [getFilesFromEvent, setFiles, onErrCb, noDragEventsBubbling]
  );

  // Fn for opening the file dialog programmatically
  const openFileDialog = useCallback(() => {
    if (fsAccessApiWorksRef.current) {
      dispatch({ type: 'openDialog' });
      onFileDialogOpenCb();

      const opts = {
        multiple,
        types: pickerTypes,
      };

      (window as any)
        .showOpenFilePicker(opts)
        .then((handles: any) => {
          if (getFilesFromEvent) {
            getFilesFromEvent(handles);
          }
        })
        .then((files: File[]) => {
          setFiles(files, null);
          dispatch({ type: 'closeDialog' });
        })
        .catch((e: Error) => {
          if (isAbort(e)) {
            onFileDialogCancelCb();
            dispatch({ type: 'closeDialog' });
          } else if (isSecurityError(e)) {
            fsAccessApiWorksRef.current = false;
            if (inputRef.current) {
              inputRef.current.value = '';
              inputRef.current.click();
            } else {
              onErrCb(
                new Error(
                  'Cannot open the file picker because the File System Access API is not supported and no <input> was provided.'
                )
              );
            }
          } else {
            onErrCb(e);
          }
        });
      return;
    }

    if (inputRef.current) {
      dispatch({ type: 'openDialog' });
      onFileDialogOpenCb();
      inputRef.current.value = '';
      inputRef.current.click();
    }
  }, [
    dispatch,
    onFileDialogOpenCb,
    onFileDialogCancelCb,
    useFsAccessApi,
    setFiles,
    onErrCb,
    pickerTypes,
    multiple,
  ]);

  // Cb to open the file dialog when SPACE/ENTER occurs on the dropzone
  const onKeyDownCb = useCallback(
    (event: React.KeyboardEvent<HTMLElement>) => {
      if (
        !rootRef.current
        || !rootRef.current.isEqualNode(event.target as Node)
      ) {
        return;
      }

      if (
        event.key === ' '
        || event.key === 'Enter'
        || event.keyCode === 32
        || event.keyCode === 13
      ) {
        event.preventDefault();
        openFileDialog();
      }
    },
    [openFileDialog]
  );

  // Update focus state for the dropzone
  const onFocusCb = useCallback(() => {
    dispatch({ type: 'focus' });
  }, []);

  const onBlurCb = useCallback(() => {
    dispatch({ type: 'blur' });
  }, []);

  // Cb to open the file dialog when click occurs on the dropzone
  const onClickCb = useCallback(() => {
    if (noClick) {
      return;
    }

    if (isIeOrEdge()) {
      setTimeout(openFileDialog, 0);
    } else {
      openFileDialog();
    }
  }, [noClick, openFileDialog]);

  const composeHandler = (fn: (() => void) | null) => (disabled ? null : fn);

  const composeKeyboardHandler = (fn: (() => void) | null) => (noKeyboard ? null : composeHandler(fn));

  const composeDragHandler = (fn: (() => void) | null) => (noDrag ? null : composeHandler(fn));

  const stopPropagation = (event: React.DragEvent<HTMLElement>) => {
    if (noDragEventsBubbling && event.target) {
      event.stopPropagation();
    }
  };

  const getRootProps = useMemo(
    () => ({
        refKey = 'ref',
        role,
        onKeyDown,
        onFocus,
        onBlur,
        onClick,
        onDragEnter: _onDragEnterProp,
        onDragOver: _onDragOverProp,
        onDragLeave: _onDragLeaveProp,
        onDrop: _onDropProp,
        ...rest
      }: GetRootPropsOptions = {}) => ({
        onKeyDown: composeKeyboardHandler(
          composeEventHandlers(onKeyDown, onKeyDownCb) as any
        ),
        onFocus: composeKeyboardHandler(
          composeEventHandlers(onFocus, onFocusCb) as any
        ),
        onBlur: composeKeyboardHandler(
          composeEventHandlers(onBlur, onBlurCb) as any
        ),
        onClick: composeHandler(
          composeEventHandlers(onClick, onClickCb) as any
        ),
        onDragEnter: composeDragHandler(
          composeEventHandlers(onDragEnter, onDragEnterCb) as any
        ),
        onDragOver: composeDragHandler(
          composeEventHandlers(onDragOver, onDragOverCb) as any
        ),
        onDragLeave: composeDragHandler(
          composeEventHandlers(onDragLeave, onDragLeaveCb) as any
        ),
        onDrop: composeDragHandler(
          composeEventHandlers(onDrop, onDropCb) as any
        ),
        role: typeof role === 'string' && role !== '' ? role : 'presentation',
        [refKey]: rootRef,
        ...(!disabled && !noKeyboard ? { tabIndex: 0 } : {}),
        ...rest,
      }),
    [
      rootRef,
      onKeyDownCb,
      onFocusCb,
      onBlurCb,
      onClickCb,
      onDragEnterCb,
      onDragOverCb,
      onDragLeaveCb,
      onDropCb,
      noKeyboard,
      noDrag,
      disabled,
    ]
  );

  const onInputElementClick = useCallback(
    (event: React.MouseEvent<HTMLElement>) => {
      event.stopPropagation();
    },
    []
  );

  const getInputProps = useMemo(
    () => ({
        refKey = 'ref',
        onChange,
        onClick,
        ...rest
      }: GetInputPropsOptions = {}) => {
        const inputProps = {
          accept: acceptAttr,
          multiple,
          type: 'file',
          style: {
            border: 0,
            clip: 'rect(0, 0, 0, 0)',
            clipPath: 'inset(50%)',
            height: '1px',
            margin: '0 -1px -1px 0',
            overflow: 'hidden',
            padding: 0,
            position: 'absolute',
            width: '1px',
            whiteSpace: 'nowrap',
          },
          onChange: composeHandler(
            composeEventHandlers(onChange, onDropCb) as any
          ),
          onClick: composeHandler(
            composeEventHandlers(onClick, onInputElementClick) as any
          ),
          tabIndex: -1,
          [refKey]: inputRef,
        };

        return {
          ...inputProps,
          ...rest,
        };
      },
    [inputRef, accept, multiple, onDropCb, disabled]
  );

  return {
    ...state,
    isFocused: isFocused && !disabled,
    getRootProps,
    getInputProps,
    rootRef,
    inputRef,
    open: composeHandler(openFileDialog) as any,
  };
}

// Action type
type DropzoneAction =
  | { type: 'focus' }
  | { type: 'blur' }
  | { type: 'openDialog' }
  | { type: 'closeDialog' }
  | {
      type: 'setDraggedFiles';
      isDragActive: boolean;
      isDragAccept: boolean;
      isDragReject: boolean;
    }
  | {
      type: 'setFiles';
      acceptedFiles: File[];
      fileRejections: FileRejection[];
      isDragReject: boolean;
    }
  | { type: 'reset' };

/**
 * Reducer function for managing dropzone state
 */
function reducer(state: DropzoneState, action: DropzoneAction): DropzoneState {
  switch (action.type) {
    case 'focus':
      return {
        ...state,
        isFocused: true,
      };
    case 'blur':
      return {
        ...state,
        isFocused: false,
      };
    case 'openDialog':
      return {
        ...initialState,
        isFileDialogActive: true,
      };
    case 'closeDialog':
      return {
        ...state,
        isFileDialogActive: false,
      };
    case 'setDraggedFiles':
      return {
        ...state,
        isDragActive: action.isDragActive,
        isDragAccept: action.isDragAccept,
        isDragReject: action.isDragReject,
      };
    case 'setFiles':
      return {
        ...state,
        acceptedFiles: action.acceptedFiles,
        fileRejections: action.fileRejections,
        isDragReject: action.isDragReject,
      };
    case 'reset':
      return {
        ...initialState,
      };
    default:
      return state;
  }
}

function noop() {}

// export { ErrorCode } from "./utils/index.js";
