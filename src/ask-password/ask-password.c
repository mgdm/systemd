/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <errno.h>
#include <getopt.h>
#include <stddef.h>
#include <unistd.h>

#include "ask-password-api.h"
#include "def.h"
#include "log.h"
#include "macro.h"
#include "main-func.h"
#include "parse-argument.h"
#include "pretty-print.h"
#include "strv.h"
#include "terminal-util.h"

static const char *arg_icon = NULL;
static const char *arg_id = NULL;               /* identifier for 'ask-password' protocol */
static const char *arg_key_name = NULL;         /* name in kernel keyring */
static const char *arg_credential_name = NULL;  /* name in $CREDENTIALS_DIRECTORY directory */
static char *arg_message = NULL;
static usec_t arg_timeout = DEFAULT_TIMEOUT_USEC;
static bool arg_multiple = false;
static bool arg_no_output = false;
static AskPasswordFlags arg_flags = ASK_PASSWORD_PUSH_CACHE;

STATIC_DESTRUCTOR_REGISTER(arg_message, freep);

static int help(void) {
        _cleanup_free_ char *link = NULL;
        int r;

        r = terminal_urlify_man("systemd-ask-password", "1", &link);
        if (r < 0)
                return log_oom();

        printf("%1$s [OPTIONS...] MESSAGE\n\n"
               "%3$sQuery the user for a system passphrase, via the TTY or an UI agent.%4$s\n\n"
               "  -h --help           Show this help\n"
               "     --icon=NAME      Icon name\n"
               "     --id=ID          Query identifier (e.g. \"cryptsetup:/dev/sda5\")\n"
               "     --keyname=NAME   Kernel key name for caching passwords (e.g. \"cryptsetup\")\n"
               "     --credential=NAME\n"
               "                      Credential name for LoadCredential=/SetCredential=\n"
               "                      credentials\n"
               "     --timeout=SEC    Timeout in seconds\n"
               "     --echo           Do not mask input (useful for usernames)\n"
               "     --emoji=yes|no|auto\n"
               "                      Show a lock and key emoji\n"
               "     --no-tty         Ask question via agent even on TTY\n"
               "     --accept-cached  Accept cached passwords\n"
               "     --multiple       List multiple passwords if available\n"
               "     --no-output      Do not print password to standard output\n"
               "\nSee the %2$s for details.\n",
               program_invocation_short_name,
               link,
               ansi_highlight(),
               ansi_normal());

        return 0;
}

static int parse_argv(int argc, char *argv[]) {

        enum {
                ARG_ICON = 0x100,
                ARG_TIMEOUT,
                ARG_ECHO,
                ARG_EMOJI,
                ARG_NO_TTY,
                ARG_ACCEPT_CACHED,
                ARG_MULTIPLE,
                ARG_ID,
                ARG_KEYNAME,
                ARG_NO_OUTPUT,
                ARG_VERSION,
                ARG_CREDENTIAL,
        };

        static const struct option options[] = {
                { "help",          no_argument,       NULL, 'h'               },
                { "version",       no_argument,       NULL, ARG_VERSION       },
                { "icon",          required_argument, NULL, ARG_ICON          },
                { "timeout",       required_argument, NULL, ARG_TIMEOUT       },
                { "echo",          no_argument,       NULL, ARG_ECHO          },
                { "emoji",         required_argument, NULL, ARG_EMOJI         },
                { "no-tty",        no_argument,       NULL, ARG_NO_TTY        },
                { "accept-cached", no_argument,       NULL, ARG_ACCEPT_CACHED },
                { "multiple",      no_argument,       NULL, ARG_MULTIPLE      },
                { "id",            required_argument, NULL, ARG_ID            },
                { "keyname",       required_argument, NULL, ARG_KEYNAME       },
                { "no-output",     no_argument,       NULL, ARG_NO_OUTPUT     },
                { "credential",    required_argument, NULL, ARG_CREDENTIAL    },
                {}
        };

        const char *emoji = NULL;
        int c;

        assert(argc >= 0);
        assert(argv);

        while ((c = getopt_long(argc, argv, "h", options, NULL)) >= 0)

                switch (c) {

                case 'h':
                        return help();

                case ARG_VERSION:
                        return version();

                case ARG_ICON:
                        arg_icon = optarg;
                        break;

                case ARG_TIMEOUT:
                        if (parse_sec(optarg, &arg_timeout) < 0)
                                return log_error_errno(SYNTHETIC_ERRNO(EINVAL),
                                                       "Failed to parse --timeout parameter %s",
                                                       optarg);
                        break;

                case ARG_ECHO:
                        arg_flags |= ASK_PASSWORD_ECHO;
                        break;

                case ARG_EMOJI:
                        emoji = optarg;
                        break;

                case ARG_NO_TTY:
                        arg_flags |= ASK_PASSWORD_NO_TTY;
                        break;

                case ARG_ACCEPT_CACHED:
                        arg_flags |= ASK_PASSWORD_ACCEPT_CACHED;
                        break;

                case ARG_MULTIPLE:
                        arg_multiple = true;
                        break;

                case ARG_ID:
                        arg_id = optarg;
                        break;

                case ARG_KEYNAME:
                        arg_key_name = optarg;
                        break;

                case ARG_NO_OUTPUT:
                        arg_no_output = true;
                        break;

                case ARG_CREDENTIAL:
                        arg_credential_name = optarg;
                        break;

                case '?':
                        return -EINVAL;

                default:
                        assert_not_reached("Unhandled option");
                }

        if (isempty(emoji) || streq(emoji, "auto"))
                SET_FLAG(arg_flags, ASK_PASSWORD_HIDE_EMOJI, FLAGS_SET(arg_flags, ASK_PASSWORD_ECHO));
        else {
                int r;
                bool b;

                r = parse_boolean_argument("--emoji=", emoji, &b);
                if (r < 0)
                         return r;
                SET_FLAG(arg_flags, ASK_PASSWORD_HIDE_EMOJI, !b);
        }

        if (argc > optind) {
                arg_message = strv_join(argv + optind, " ");
                if (!arg_message)
                        return log_oom();
        }

        return 1;
}

static int run(int argc, char *argv[]) {
        _cleanup_strv_free_erase_ char **l = NULL;
        usec_t timeout;
        char **p;
        int r;

        log_show_color(true);
        log_parse_environment();
        log_open();

        r = parse_argv(argc, argv);
        if (r <= 0)
                return r;

        if (arg_timeout > 0)
                timeout = usec_add(now(CLOCK_MONOTONIC), arg_timeout);
        else
                timeout = 0;

        r = ask_password_auto(arg_message, arg_icon, arg_id, arg_key_name, arg_credential_name ?: "password", timeout, arg_flags, &l);
        if (r < 0)
                return log_error_errno(r, "Failed to query password: %m");

        STRV_FOREACH(p, l) {
                if (!arg_no_output)
                        puts(*p);

                if (!arg_multiple)
                        break;
        }

        return 0;
}

DEFINE_MAIN_FUNCTION(run);
