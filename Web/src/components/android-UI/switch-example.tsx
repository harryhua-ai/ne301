import { useState } from 'preact/hooks';
import AndroidSwitch from './switch';

// Usage example component
export default function AndroidSwitchExample() {
  const [isEnabled, setIsEnabled] = useState(false);
  const [isDisabled, setIsDisabled] = useState(false);

  return (
    <div className="p-4 space-y-4">
      <h2 className="text-lg font-semibold">Android Switch Example</h2>
      
      {/* Basic usage */}
      <div className="flex items-center space-x-3">
        <label className="text-sm font-medium">Basic switch:</label>
        <AndroidSwitch
          checked={isEnabled}
          onCheckedChange={setIsEnabled}
          aria-label="Basic switch"
        />
        <span className="text-sm text-gray-600">
          {isEnabled ? 'On' : 'Off'}
        </span>
      </div>

      {/* Disabled state */}
      <div className="flex items-center space-x-3">
        <label className="text-sm font-medium">Disabled state:</label>
        <AndroidSwitch
          checked={isDisabled}
          onCheckedChange={setIsDisabled}
          disabled
          aria-label="Disabled switch"
        />
        <span className="text-sm text-gray-600">Disabled        </span>
      </div>

      {/* Custom styles */}
      <div className="flex items-center space-x-3">
        <label className="text-sm font-medium">Custom styles:</label>
        <AndroidSwitch
          checked={isEnabled}
          onCheckedChange={setIsEnabled}
          className="h-8 w-14" // Larger size
          aria-label="Custom switch"
        />
      </div>

      {/* Controlled component example */}
      <div className="flex items-center space-x-3">
        <label className="text-sm font-medium">Controlled component:</label>
        <AndroidSwitch
          checked={isEnabled}
          onCheckedChange={(checked) => {
            console.log('Switch state changed:', checked);
            setIsEnabled(checked);
          }}
          aria-label="Controlled switch"
        />
      </div>
    </div>
  );
}
