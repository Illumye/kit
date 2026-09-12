#define UTILS_NO_VEC_MATH
#define UTILS_IMPLEMENTATION
#include "../utils.h"

int main(int argc, char **argv) {
    const char *prog = args_shift(&argc, &argv); /* consomme argv[0] */

    /* Defaults */
    bool        help     = false;
    bool        verbose  = false;
    bool        dry_run  = false;
    const char *output   = "a.out";
    const char *format   = "text";
    int         jobs     = 4;

    Opt opts[] = {
        OPT_FLAG('h', "help",     "Afficher cette aide",           &help),
        OPT_FLAG('v', "verbose",  "Afficher les détails",          &verbose),
        OPT_FLAG('n', "dry-run",  "Simuler sans écrire",           &dry_run),
        OPT_STR ('o', "output",   "FILE", "Fichier de sortie",     &output),
        OPT_STR ('f', "format",   "FMT",  "Format (text/json/csv)",&format),
        OPT_INT ('j', "jobs",     "N",    "Jobs parallèles",       &jobs),
    };

    if (!opts_parse_arr(opts, &argc, &argv)) {
        opts_usage_arr(stderr, prog, opts);
        return 1;
    }

    if (help || argc == 0) {
        opts_usage_arr(stdout, prog, opts);
        return 0;
    }

    /* Utilisation des valeurs parsées */
    if (verbose) {
        LOG(LOG_INFO, "verbose   : on");
        LOG(LOG_INFO, "dry-run   : %s", dry_run ? "on" : "off");
        LOG(LOG_INFO, "output    : %s", output);
        LOG(LOG_INFO, "format    : %s", format);
        LOG(LOG_INFO, "jobs      : %d", jobs);
        LOG(LOG_INFO, "fichiers (%d) :", argc);
        for (int i = 0; i < argc; i++)
            LOG(LOG_INFO, "  [%d] %s", i, argv[i]);
    }

    if (dry_run) {
        printf("[dry-run] would write to '%s' with format '%s' using %d job(s)\n",
               output, format, jobs);
        return 0;
    }

    for (int i = 0; i < argc; i++)
        printf("traitement de '%s' -> '%s' (format=%s, jobs=%d)\n",
               argv[i], output, format, jobs);

    return 0;
}
