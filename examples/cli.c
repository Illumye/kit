#define KIT_NO_VEC_MATH
#define KIT_IMPLEMENTATION
#include "../kit.h"

int main(int argc, char **argv) {
    const char *prog = kit_cli_shift(&argc, &argv); /* consomme argv[0] */

    /* Defaults */
    bool        help     = false;
    bool        verbose  = false;
    bool        dry_run  = false;
    const char *output   = "a.out";
    const char *format   = "text";
    int         jobs     = 4;

    KitCliOpt opts[] = {
        KIT_CLI_FLAG('h', "help",     "Afficher cette aide",           &help),
        KIT_CLI_FLAG('v', "verbose",  "Afficher les détails",          &verbose),
        KIT_CLI_FLAG('n', "dry-run",  "Simuler sans écrire",           &dry_run),
        KIT_CLI_STR ('o', "output",   "FILE", "Fichier de sortie",     &output),
        KIT_CLI_STR ('f', "format",   "FMT",  "Format (text/json/csv)",&format),
        KIT_CLI_INT ('j', "jobs",     "N",    "Jobs parallèles",       &jobs),
    };

    if (!kit_cli_parse_arr(opts, &argc, &argv)) {
        kit_cli_usage_arr(stderr, prog, opts);
        return 1;
    }

    if (help || argc == 0) {
        kit_cli_usage_arr(stdout, prog, opts);
        return 0;
    }

    /* Utilisation des valeurs parsées */
    if (verbose) {
        KIT_INFO("verbose   : on");
        KIT_INFO("dry-run   : %s", dry_run ? "on" : "off");
        KIT_INFO("output    : %s", output);
        KIT_INFO("format    : %s", format);
        KIT_INFO("jobs      : %d", jobs);
        KIT_INFO("fichiers (%d) :", argc);
        for (int i = 0; i < argc; i++)
            KIT_INFO("  [%d] %s", i, argv[i]);
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
