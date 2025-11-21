import { useState } from 'preact/hooks';
import Upload from '@/components/upload';
import { useLingui } from '@lingui/react';
import SvgIcon from '@/components/svg-icon';
import systemApis from '@/services/api/system';
import { Button } from '@/components/ui/button';

export default function ImportFSBL() {
    const { i18n } = useLingui();
    const { uploadOTAFileReq, updateOTAReq, restartDevice } = systemApis;
    const [fsblFile, setfsblFile] = useState<File | null>(null)

    const uploadFSBL = async (file: File) => {
        try {
            setfsblFile(file);
            await uploadOTAFileReq(file, 'fsbl');
            await updateOTAReq({
                filename: file.name,
                firmware_type: 'fsbl',
                validate_crc32: true,
                validate_signature: true,
                allow_downgrade: true,
                auto_upgrade: true
            });
        } catch (error) {
            console.error('uploadFSBL', error);
            throw error;
        }
    }

    const handleUpdate = async () => {
        try {
            restartDevice({ delay_seconds: 1 });
        } catch (error) {
            console.error('handleUpdate', error);
            throw error;
        }
    }

    const customUpload = ({ placeholder, fileName }: { placeholder: string, fileName: string }) => (
        <div className="flex flex-col gap-2 flex-1 items-center justify-center w-full h-full rounded-md">
            {
                fileName ? (
                    <div className="flex flex-col items-center gap-2">
                        <div className="w-14 h-14 bg-gray-400 rounded-md flex items-center justify-center">
                            <SvgIcon className="w-10 h-10" icon="file" />
                        </div>
                        <p className="text-sm items-center  text-wrap text-text-primary">{fileName}</p>
                    </div>
                ) : (
                    <div className="flex flex-col gap-2 flex-1 items-center justify-center w-full h-full">
                        <SvgIcon className="w-10 h-10" icon="upload_single" />
                        <p className="text-sm items-center  text-wrap text-text-secondary">{placeholder}</p>
                    </div>
                )
            }
        </div>
    )
    return (
        <div className="w-full h-full flex justify-center pt-4">
            <div className="md:max-w-xl mx-4 w-full flex flex-col gap-2">
                <Upload
                  className="h-50"
                  type="customZone"
                  slot={customUpload({ placeholder: i18n._('sys.system_management.fsbl_file'), fileName: fsblFile?.name || '' })}
                  accept={{
                        'application/octet-stream': ['.bin'],
                    }}
                  maxFiles={1}
                  maxSize={1024 * 1024 * 10}
                  multiple={false}
                  onFileChange={uploadFSBL}
                />

                <div className="flex gap-2 justify-end">
                    <Button
                      variant="primary"
                      className="w-1/2 md:w-auto"
                      onClick={() => handleUpdate()}
                    >
                        {i18n._('sys.system_management.confirm_burn')}
                    </Button>
                </div>
            </div>
        </div>
    );
}