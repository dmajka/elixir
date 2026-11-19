/** Hardware-validated slot1/build1563 subset. Every write requires a fresh backup and bound user confirmation. */
export const XL3_UPLOAD_EXPERIMENT_ENABLED = true
export const XL3_UPLOAD_LIMIT = 'Direct upload supports firmware build 1563 and absolute CC mappings. Download the backup and confirm before overwriting.'
export function isXL3UploadEnabled(): boolean { return XL3_UPLOAD_EXPERIMENT_ENABLED }
