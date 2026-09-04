module.exports = {
    uiPort: process.env.PORT || 1880,

    // Keep flow context on disk so pending Supabase writes survive a
    // Node-RED or laptop restart.
    contextStorage: {
        default: {
            module: "localfilesystem"
        }
    },

    flowFilePretty: true,
    functionExternalModules: false,
    editorTheme: {
        projects: { enabled: false }
    },
    logging: {
        console: {
            level: "info",
            metrics: false,
            audit: false
        }
    }
};
