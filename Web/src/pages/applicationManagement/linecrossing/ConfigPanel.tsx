import { useLingui } from '@lingui/react';
import { Card, CardContent } from '@/components/ui/card';
import { Label } from '@/components/ui/label';
import { Input } from '@/components/ui/input';
import { Switch } from '@/components/ui/switch';
import { Button } from '@/components/ui/button';
import type { LineCrossingEditorConfig } from './lineCrossingAdapter';

export interface ConfigPanelProps {
  config: LineCrossingEditorConfig;
  modelName: string;
  onChange: (patch: Partial<LineCrossingEditorConfig>) => void;
  onSave: () => void;
  saving: boolean;
  saveDisabled: boolean;
  editMode: boolean;
  editPhase: 0 | 1 | 2;
  hasLine: boolean;
  generationDrift: boolean;
  targetChanged: boolean;
  destructiveAck: boolean;
  onDestructiveAckChange: (value: boolean) => void;
  validationReason: string | null;
  onToggleEdit: () => void;
  onResetLine: () => void;
  onFlipDirection: () => void;
}

function permilleToPercent(value: number): string {
  return `${(value / 10).toFixed(1)}%`;
}

export default function ConfigPanel({
  config,
  modelName,
  onChange,
  onSave,
  saving,
  saveDisabled,
  editMode,
  editPhase,
  hasLine,
  generationDrift,
  targetChanged,
  destructiveAck,
  onDestructiveAckChange,
  validationReason,
  onToggleEdit,
  onResetLine,
  onFlipDirection,
}: ConfigPanelProps) {
  const { i18n } = useLingui();

  const phaseHintKey = !editMode
    ? null
    : editPhase === 0
      ? 'sys.pc.hint_click_l1'
      : editPhase === 1
        ? 'sys.pc.hint_click_l2'
        : 'sys.pc.hint_done';

  const numberField = (
    key: keyof LineCrossingEditorConfig,
    labelKey: string,
    min?: number,
    max?: number
  ) => (
    <div className="flex flex-col gap-1">
      <Label>{i18n._(labelKey)}</Label>
      <Input
        type="number"
        min={min}
        max={max}
        value={config[key] as number}
        onChange={event => onChange({ [key]: Number((event.target as HTMLInputElement).value) })}
      />
    </div>
  );

  const permilleField = (
    key: keyof LineCrossingEditorConfig,
    labelKey: string
  ) => (
    <div className="flex flex-col gap-1">
      <Label>{i18n._(labelKey)}</Label>
      <Input
        type="number"
        min={0}
        max={1000}
        value={config[key] as number}
        onChange={event => onChange({ [key]: Number((event.target as HTMLInputElement).value) })}
      />
      <span className="text-xs text-gray-400">
        {permilleToPercent(config[key] as number)}
      </span>
    </div>
  );

  const switchField = (
    key: keyof LineCrossingEditorConfig,
    labelKey: string
  ) => (
    <div className="flex items-center justify-between">
      <Label>{i18n._(labelKey)}</Label>
      <Switch
        checked={config[key] as boolean}
        onCheckedChange={value => onChange({ [key]: value })}
      />
    </div>
  );

  return (
    <Card data-testid="pc-source-config">
      <CardContent className="space-y-5 p-4">
        {generationDrift && (
          <div
            className="rounded border border-amber-300 bg-amber-50 p-2 text-xs text-amber-800"
            data-testid="lc-generation-drift"
          >
            {i18n._('sys.app_management.business.generation_drift')}
          </div>
        )}
        <div className="space-y-2.5" data-testid="pc-group-general">
          <h4 className="text-sm font-semibold text-gray-800">
            {i18n._('sys.pc.group_general')}
          </h4>
          <div className="flex flex-col gap-1">
            <Label>{i18n._('sys.app_management.business.counter_name')}</Label>
            <Input
              type="text"
              maxLength={63}
              value={config.counter_name}
              onChange={event => onChange({
                  counter_name: (event.target as HTMLInputElement).value,
                })}
              data-testid="lc-counter-name"
            />
          </div>
        </div>
        <div className="space-y-2.5" data-testid="pc-group-line">
          <h4 className="text-sm font-semibold text-gray-800">
            {i18n._('sys.pc.group_line')}
          </h4>
          <div className="flex flex-wrap items-center gap-3">
            <Button
              size="sm"
              variant={editMode ? 'primary' : 'outline'}
              onClick={onToggleEdit}
              data-testid="pc-edit-line"
            >
              {editMode
                ? i18n._('sys.pc.edit_done')
                : i18n._('sys.pc.edit_line')}
            </Button>
            <Button
              size="sm"
              variant="outline"
              onClick={onFlipDirection}
              disabled={!hasLine}
              data-testid="pc-flip-direction"
            >
              {i18n._('sys.pc.flip_direction')}
            </Button>
            <Button
              size="sm"
              variant="outline"
              onClick={onResetLine}
              data-testid="pc-reset-line"
            >
              {i18n._('sys.pc.reset_line')}
            </Button>
          </div>
          {phaseHintKey && (
            <p className="text-xs leading-snug text-amber-600">
              {i18n._(phaseHintKey)}
            </p>
          )}
          <div className="grid grid-cols-3 gap-3">
            {permilleField('line_x1_permille', 'sys.pc.line_x1')}
            {permilleField('line_y1_permille', 'sys.pc.line_y1')}
            {permilleField('line_x2_permille', 'sys.pc.line_x2')}
            {permilleField('line_y2_permille', 'sys.pc.line_y2')}
            {permilleField('outside_x_permille', 'sys.pc.outside_x')}
            {permilleField('outside_y_permille', 'sys.pc.outside_y')}
          </div>
        </div>
        <div className="space-y-2.5" data-testid="pc-group-detection">
          <h4 className="text-sm font-semibold text-gray-800">
            {i18n._('sys.pc.group_detection')}
          </h4>
          <div className="grid grid-cols-2 gap-3">
            {permilleField('conf_threshold_permille', 'sys.pc.conf_threshold')}
            {permilleField('max_dist_permille', 'sys.pc.max_dist')}
            <div className="flex flex-col gap-1">
              <Label>
                {i18n._('sys.app_management.business.target_class')}
              </Label>
              <Input
                type="text"
                maxLength={31}
                value={config.target_class_name}
                onChange={event => onChange({
                    target_class_name: (event.target as HTMLInputElement).value,
                  })}
                data-testid="lc-target-class"
              />
            </div>
            <div className="flex flex-col gap-1">
              <Label>{i18n._('sys.pc.model_name')}</Label>
              <Input value={modelName || '—'} readOnly />
            </div>
          </div>
        </div>
        <div className="space-y-2.5" data-testid="pc-group-tracking">
          <h4 className="text-sm font-semibold text-gray-800">
            {i18n._('sys.pc.group_tracking')}
          </h4>
          <div className="grid grid-cols-3 gap-3">
            {numberField('track_history_k', 'sys.pc.track_history_k', 4, 16)}
            {numberField('max_miss', 'sys.pc.max_miss', 1)}
            {numberField('k_confirm', 'sys.pc.k_confirm', 1)}
          </div>
        </div>
        <div className="space-y-2.5" data-testid="pc-group-reporting">
          <h4 className="text-sm font-semibold text-gray-800">
            {i18n._('sys.pc.group_reporting')}
          </h4>
          <div className="grid grid-cols-2 gap-3">
            {numberField('window_minutes', 'sys.pc.window_minutes', 1, 1440)}
          </div>
          <div className="space-y-2">
            {switchField('tracks_report_enable', 'sys.pc.tracks_report')}
            {switchField('heat_grid_enable', 'sys.pc.heat_grid')}
          </div>
        </div>
        {validationReason && (
          <p className="text-xs text-red-600" data-testid="lc-field-error">
            {i18n._('sys.app_management.business.invalid_field')}:{' '}
            {validationReason}
          </p>
        )}
        {targetChanged && (
          <label
            className="flex items-start gap-2 rounded border border-[#f0d8be] bg-[#fff8f1] p-2 text-xs text-[#745638]"
            data-testid="lc-destructive-ack"
          >
            <input
              type="checkbox"
              checked={destructiveAck}
              onChange={event => onDestructiveAckChange(
                  (event.target as HTMLInputElement).checked
                )}
              data-testid="lc-destructive-checkbox"
            />
            <span>{i18n._('sys.app_management.business.destructive_ack')}</span>
          </label>
        )}
        <Button
          variant="primary"
          size="sm"
          className="mt-2 w-full"
          onClick={onSave}
          disabled={saving || saveDisabled}
          data-testid="lc-save"
        >
          {saving ? i18n._('sys.pc.saving') : i18n._('sys.pc.save')}
        </Button>
      </CardContent>
    </Card>
  );
}
