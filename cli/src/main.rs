use std::process;

use clap::{Parser, Subcommand};

mod commands;
mod ipc;

#[derive(Parser)]
#[command(name = "typio")]
#[command(about = "Typio command-line interface")]
#[command(version)]
struct Cli {
    #[command(subcommand)]
    command: Option<Commands>,
}

#[derive(Subcommand)]
enum Commands {
    /// Query or switch keyboard engine
    #[command(name = "engine")]
    Engine {
        /// Subcommand or engine name
        args: Vec<String>,
    },

    /// Query, deploy, or set Rime schema
    #[command(name = "rime")]
    Rime {
        /// Subcommand or schema name
        args: Vec<String>,
    },

    /// Manage configuration
    #[command(name = "config")]
    Config {
        /// Subcommand
        args: Vec<String>,
    },

    /// Show server status
    #[command(name = "status")]
    Status,

    /// Stop the Typio server
    #[command(name = "stop")]
    Stop,

    /// Show server version
    #[command(name = "version")]
    Version,
}

fn print_help(prog: &str) {
    println!("Usage: {} <command> [args...]\n", prog);
    println!("Commands:");
    println!("  engine [list|next|NAME]  Query or switch keyboard engine");
    println!("  rime [schema|deploy]     Query, deploy, or set Rime schema");
    println!("  config <reload|get|set>  Manage configuration");
    println!("  status                   Show server status");
    println!("  stop                     Stop the Typio server");
    println!("  version                  Show server version");
    println!("  help                     Show this help message");
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let prog = args.first().map(|s| s.as_str()).unwrap_or("typio");

    // If no arguments, print help
    if args.len() < 2 {
        print_help(prog);
        process::exit(1);
    }

    // Handle explicit help flags before clap parsing
    let second = args[1].as_str();
    if second == "help" || second == "--help" || second == "-h" {
        print_help(prog);
        process::exit(0);
    }

    // Parse with clap.
    let cli = match Cli::try_parse() {
        Ok(c) => c,
        Err(e) => {
            // If the user typed an unknown command, clap will error.
            // Print our custom help instead of clap's verbose error.
            if args.len() >= 2 {
                let cmd = &args[1];
                let known = ["engine", "rime", "config", "status", "stop", "version"];
                if !known.contains(&cmd.as_str()) {
                    eprintln!("typio: unknown command: {}", cmd);
                    print_help(prog);
                    process::exit(1);
                }
            }
            eprintln!("{}", e);
            process::exit(1);
        }
    };

    let result = match cli.command {
        Some(Commands::Engine { args }) => {
            commands::cmd_engine(args.first().map(|s| s.as_str()))
        }
        Some(Commands::Rime { args }) => {
            commands::cmd_rime(args.first().map(|s| s.as_str()), &args[1..])
        }
        Some(Commands::Config { args }) => {
            commands::cmd_config(args.first().map(|s| s.as_str()), &args[1..])
        }
        Some(Commands::Status) => commands::cmd_status(),
        Some(Commands::Stop) => commands::cmd_stop(),
        Some(Commands::Version) => commands::cmd_version(),
        None => {
            print_help(prog);
            Ok(())
        }
    };

    if let Err(e) = result {
        eprintln!("typio: {}", e);
        process::exit(1);
    }
}
