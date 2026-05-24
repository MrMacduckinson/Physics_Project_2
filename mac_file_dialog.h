#ifndef MAC_FILE_DIALOG_H
#define MAC_FILE_DIALOG_H

#ifdef __cplusplus
extern "C" {
#endif

// Returns a pointer to an internal static buffer, or NULL if canceled.
const char* mac_file_dialog(const char* title, const char* filters, const char* initial_dir);

#ifdef __cplusplus
}
#endif

#endif

