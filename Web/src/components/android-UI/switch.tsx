import React, { useState, useRef, useEffect } from 'react';

interface AndroidSwitchProps {
  checked?: boolean;
  onCheckedChange?: (checked: boolean) => void;
  disabled?: boolean;
  className?: string;
  'aria-label'?: string;
  'aria-describedby'?: string;
}

const AndroidSwitch = React.forwardRef<HTMLButtonElement, AndroidSwitchProps>(
  ({ checked = false, onCheckedChange, disabled = false, className = '', ...props }, ref) => {
    const [isChecked, setIsChecked] = useState(checked);
    const [isPressed, setIsPressed] = useState(false);
    const buttonRef = useRef<HTMLButtonElement>(null);

    // Merge refs
    useEffect(() => {
      if (ref) {
        if (typeof ref === 'function') {
          ref(buttonRef.current);
        } else {
          ref.current = buttonRef.current;
        }
      }
    }, [ref]);

    // Sync external checked state
    useEffect(() => {
      setIsChecked(checked);
    }, [checked]);

    const handleClick = () => {
      if (disabled) return;
      
      const newChecked = !isChecked;
      setIsChecked(newChecked);
      onCheckedChange?.(newChecked);
    };

    const handleTouchStart = (e: React.TouchEvent<HTMLButtonElement>) => {
      if (disabled) return;
      e.preventDefault();
      setIsPressed(true);
    };

    const handleTouchEnd = (e: React.TouchEvent<HTMLButtonElement>) => {
      e.preventDefault();
      setIsPressed(false);
    };

    const handleKeyDown = (e: React.KeyboardEvent<HTMLButtonElement>) => {
      if (disabled) return;
      
      if (e.key === ' ' || e.key === 'Enter') {
        e.preventDefault();
        handleClick();
      }
    };

    // Base style class names
    const baseClasses = [
      "relative",
      "inline-flex", 
      "h-6",
      "w-11",
      "shrink-0",
      "cursor-pointer",
      "items-center",
      "rounded-full",
      "border-2",
      "border-transparent",
      "transition-all",
      "duration-200",
      "ease-in-out"
    ];

    // Focus styles
    const focusClasses = [
      "focus:outline-none",
      "focus:ring-2", 
      "focus:ring-blue-500",
      "focus:ring-offset-2"
    ];

    // Disabled styles
    const disabledClasses = [
      "disabled:cursor-not-allowed",
      "disabled:opacity-50"
    ];

    // Background color state
    const backgroundClasses = isChecked 
      ? ["bg-blue-600"] 
      : ["bg-gray-300"];

    // Press effect
    const pressedClasses = isPressed && !disabled ? ["scale-95"] : [];

    // Merge all styles
    const buttonClasses = [
      ...baseClasses,
      ...focusClasses,
      ...disabledClasses,
      ...backgroundClasses,
      ...pressedClasses,
      className
    ].join(' ');

    // Thumb styles
    const thumbClasses = [
      "pointer-events-none",
      "block",
      "h-5",
      "w-5", 
      "rounded-full",
      "bg-white",
      "shadow-lg",
      "transition-transform",
      "duration-200",
      "ease-in-out",
      isChecked ? "translate-x-5" : "translate-x-0"
    ].join(' ');

    return (
      <button
        ref={buttonRef}
        type="button"
        role="switch"
        aria-checked={isChecked}
        aria-disabled={disabled}
        disabled={disabled}
        className={buttonClasses}
        onClick={handleClick}
        onTouchStart={handleTouchStart}
        onTouchEnd={handleTouchEnd}
        onKeyDown={handleKeyDown}
        {...props}
      >
        {/* Thumb */}
        <span className={thumbClasses} />
      </button>
    );
  }
);

AndroidSwitch.displayName = 'AndroidSwitch';

export { AndroidSwitch };
export default AndroidSwitch;