<script setup>
import { ref, watch } from 'vue';

const props = defineProps({
  name: String, label: String, modelValue: Number, min: Number, max: Number,
  step: { type: Number, default: 1 }, unit: { type: String, default: '' },
  disabled: Boolean,
});
const emit = defineEmits(['update:modelValue']);
const displayValue = (value) => Number.isFinite(value) ? String(Number(value.toFixed(5))) : '';
const draft = ref(displayValue(props.modelValue));
const editing = ref(false);
const invalid = ref(false);
let drag;
watch(() => props.modelValue, (value) => {
  if (!editing.value) draft.value = displayValue(value);
});
watch(() => props.disabled, (disabled) => {
  if (disabled) { editing.value = false; invalid.value = false; draft.value = displayValue(props.modelValue); }
});
function apply(value) {
  const decimals = (String(props.step).split('.')[1] ?? '').length;
  const next = Number(Math.min(props.max, Math.max(props.min, value)).toFixed(Math.max(decimals, 5)));
  draft.value = String(next);
  invalid.value = false;
  if (next !== props.modelValue) emit('update:modelValue', next);
}
function commit(event) {
  if (props.disabled) return;
  if (draft.value.trim() === '' || !Number.isFinite(Number(draft.value)) ||
      Number(draft.value) < props.min || Number(draft.value) > props.max ||
      (props.step === 1 && !Number.isInteger(Number(draft.value)))) {
    invalid.value = true;
    event?.target.setCustomValidity(`Enter ${props.step === 1 ? 'a whole number' : 'a number'} from ${props.min} to ${props.max}.`);
    event?.target.reportValidity();
    return;
  }
  event?.target.setCustomValidity('');
  editing.value = false;
  apply(Number(draft.value));
}
function startDrag(event) {
  if (props.disabled || event.button !== 0) return;
  drag = { x: event.clientX, value: props.modelValue };
  event.currentTarget.setPointerCapture(event.pointerId);
}
function moveDrag(event) {
  if (!drag) return;
  const distance = event.clientX - drag.x;
  apply(drag.value + Math.round(distance / (event.shiftKey ? 24 : 6)) * props.step);
}
</script>

<template>
  <div class="number-control" :class="{ disabled }">
    <label :for="name" class="number-label" title="Drag left or right to adjust; Shift for finer movement"
      @pointerdown="startDrag" @pointermove="moveDrag" @pointerup="drag = null" @pointercancel="drag = null">
      {{ label }}<span v-if="unit" class="unit">{{ unit }}</span>
    </label>
    <div class="number-stepper">
      <button type="button" :aria-label="`Decrease ${label}`" :disabled="disabled || modelValue <= min"
        @click="apply(modelValue - step)">−</button>
      <input :id="name" :name="name" type="number" :min="min" :max="max" step="any"
        :disabled="disabled" :aria-invalid="invalid" :value="draft" autocomplete="off"
        @focus="editing = true" @input="draft = $event.target.value; invalid = false; $event.target.setCustomValidity('')"
        @change="commit" @blur="commit" @keydown.enter="$event.target.blur()"
        @keydown.esc="draft = String(modelValue); invalid = false; $event.target.setCustomValidity(''); $event.target.blur()" />
      <button type="button" :aria-label="`Increase ${label}`" :disabled="disabled || modelValue >= max"
        @click="apply(modelValue + step)">+</button>
    </div>
    <small v-if="invalid" class="field-error">Enter {{ min }}–{{ max }}{{ step === 1 ? ', whole numbers only' : '' }}.</small>
  </div>
</template>
