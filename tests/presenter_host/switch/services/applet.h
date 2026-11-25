typedef enum { AppletOperationMode_Handheld, AppletOperationMode_Console } AppletOperationMode;
typedef enum { AppletType_Application, AppletType_SystemApplication } AppletType;
static AppletOperationMode test_mode = AppletOperationMode_Console;
static inline AppletOperationMode appletGetOperationMode(void) { return test_mode; }
static inline AppletType appletGetAppletType(void) { return AppletType_Application; }
