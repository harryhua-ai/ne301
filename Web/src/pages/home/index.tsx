import TimePicker from '@/components/time-picker';
import { useState } from 'preact/hooks';

export default function Home() {
  const [value, setValue] = useState('10:00');
  return (
    <div className="p-4 w-[200px] h-[100px]">
        <TimePicker timeType="12h" value={value} onChange={setValue} />
    </div>
  )
}