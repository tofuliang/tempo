#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "raop.h"

static raop_session_t *g_session;

static void sighandler(int sig)
{
    (void)sig;
    printf("\nCaught signal, stopping...\n");
    if (g_session) raop_session_stop(g_session);
    if (g_session) raop_session_free(g_session);
    _exit(0);
}

static void state_cb(raop_session_t *s, raop_state_t state, void *ud)
{
    const char *names[] = {"STOPPED","CONNECTED","STREAMING","PAUSED","FAILED"};
    printf("\n[STATE] %s\n> ", names[state]);
    fflush(stdout);
}

static void print_help(void)
{
    printf(
        "\nCommands:\n"
        "  v <0-100>          Set volume (percent)\n"
        "  p                  Pause\n"
        "  r                  Resume\n"
        "  s <seconds>        Seek to position\n"
        "  m <title>|<artist>|<album>  Set metadata\n"
        "  pos                Show current position\n"
        "  q                  Quit\n"
        "  h                  Help\n\n");
}

static void cmd_loop(void)
{
    char line[512];

    sleep(2);
    print_help();

    while (1) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin))
            break;

        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0')
            continue;

        if (strcmp(line, "q") == 0) {
            break;
        } else if (strcmp(line, "h") == 0) {
            print_help();
        } else if (strcmp(line, "p") == 0) {
            raop_session_pause(g_session);
            printf("Pause sent\n");
        } else if (strcmp(line, "r") == 0) {
            raop_session_resume(g_session);
            printf("Resume sent\n");
        } else if (strncmp(line, "v ", 2) == 0) {
            int pct = atoi(line + 2);
            raop_session_set_volume(g_session, pct);
            printf("Volume set to %d%%\n", pct);
        } else if (strncmp(line, "s ", 2) == 0) {
            uint32_t pos_ms = (uint32_t)(atof(line + 2) * 1000);
            raop_session_seek(g_session, pos_ms);
            printf("Seek to %.1fs\n", pos_ms / 1000.0);
        } else if (strncmp(line, "m ", 2) == 0) {
            raop_metadata_t meta = {0};
            char buf[512];
            strncpy(buf, line + 2, sizeof(buf) - 1);
            meta.title = strtok(buf, "|");
            meta.artist = strtok(NULL, "|");
            meta.album = strtok(NULL, "|");
            if (!meta.title) meta.title = "Unknown";
            raop_session_set_metadata(g_session, &meta);
            printf("Metadata: %s - %s [%s]\n",
                   meta.artist ? meta.artist : "?",
                   meta.title ? meta.title : "?",
                   meta.album ? meta.album : "?");
        } else if (strcmp(line, "pos") == 0) {
            uint32_t pos = raop_session_get_position_ms(g_session);
            uint32_t dur = raop_session_get_duration_ms(g_session);
            printf("Position: %.1f / %.1fs\n", pos / 1000.0, dur / 1000.0);
        } else {
            printf("Unknown command. Type 'h' for help.\n");
        }
    }
}

int main(int argc, char **argv)
{
    const char *host = NULL;
    int port = 7000;
    const char *file = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) host = argv[++i];
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) file = argv[++i];
    }

    if (!host) {
        fprintf(stderr, "Usage: %s --host <ip> [--port <port>] [--file <audio>]\n", argv[0]);
        return 1;
    }

    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGPIPE, SIG_IGN);

    g_session = raop_session_new(host, (uint16_t)port, state_cb, NULL);
    if (!g_session) { fprintf(stderr, "Failed to create session\n"); return 1; }

    printf("Playing on %s:%d%s%s\n", host, port, file ? " file: " : "", file ? file : "");
    if (raop_session_play(g_session, file, 0) < 0) {
        fprintf(stderr, "Play failed\n");
        raop_session_free(g_session);
        return 1;
    }

    cmd_loop();

    printf("Stopping...\n");
    raop_session_stop(g_session);
    raop_session_free(g_session);
    printf("Done\n");
    return 0;
}
