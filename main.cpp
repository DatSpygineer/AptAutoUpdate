#include <iostream>
#include <chrono>
#include <unistd.h>

extern "C" {
	#include <fcntl.h>
	#include <termios.h>
}

enum class PromptResult {
	Confirmed,
	Cancelled,
	Pending
};

static int TimeLeft = 5;

static std::optional<char> getchar_opt() {
	if (int c = getchar(); c != EOF) {
		return { static_cast<char>(c) };
	}
	return std::nullopt;
}

static PromptResult prompt() {
	auto result = PromptResult::Pending;

	puts("\x1B[2J\x1B[HWould you like to run self-update? (Y/N)");
	if (auto resp = getchar_opt(); resp.has_value()) {
		if (std::tolower(*resp) == 'y') {
			result = PromptResult::Confirmed;
		} else if (std::tolower(*resp) == 'n') {
			result = PromptResult::Cancelled;
		}
	}

	if (result == PromptResult::Pending) {
		printf("Time left: %ds\n", TimeLeft);
		TimeLeft--;
		if (TimeLeft <= 0) {
			result = PromptResult::Cancelled;
		}
	}

	return result;
}

static std::optional<std::string> run_command(const std::string& command) {
	FILE* pipe = popen(command.c_str(), "r");
	if (!pipe) {
		printf("\x1B[31;1mError while running command \"%s\"\x1B[0m\n", command.c_str());
		return std::nullopt;
	}

	try {
		std::string result;
		int c = fgetc(pipe);
		while (c != EOF) {
			result += static_cast<char>(c);
			c = fgetc(pipe);
		}
		pclose(pipe);
		return result;
	} catch (const std::exception& e) {
		return std::nullopt;
	}
}

static bool run_update() {
	if (system("sudo apt update") != 0) {
		puts("\x1B[31;1mFailed to run command \"sudo apt update\"!\x1B[0m");
		return false;
	}

	if (auto updates = run_command("apt list --upgradable"); updates.has_value()) {
		std::istringstream iss;
		iss.str(updates.value());
		std::string temp;
		int update_count = 0;
		bool first = true;
		while (std::getline(iss, temp, '\n')) {
			if (first) {
				first = false;
			} else {
				printf("\x1B[34m >> Update available: %s\x1B[0m\n", temp.c_str());
				update_count++;
			}
		}

		if (update_count == 0) {
			puts("\x1B[32;1mEverything is up to date!\x1B[0m");
			return true;
		}

		printf("%d update(s) available\nPress any keys to continue...\n", update_count);
		getchar();

		if (system("sudo apt upgrade -y") != 0) {
			puts("\x1B[31;1mFailed to run command \"sudo apt upgrade -y\"!\x1B[0m");
			return false;
		}
	} else {
		puts("\x1B[31;1mFailed to run command \"apt list --upgradable\"!\x1B[0m");
		return false;
	}
	return true;
}

int main() {
	int result = 0;
	// Make stdin non-blocking
	int stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, stdin_flags | O_NONBLOCK);

	// Disable echo and canonical mode for immediate character fetch.
	termios old_settings = { }, new_settings = { };
	tcgetattr(STDIN_FILENO, &old_settings);
	new_settings = old_settings;
	new_settings.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &new_settings);

	auto time_last = std::chrono::system_clock::now();

	switch (prompt()) {
		case PromptResult::Confirmed: result = run_update() ? 0 : 1; goto END;
		case PromptResult::Cancelled: goto END;
		default: /* Fall through if pending. */ break;
	}

	while (true) {
		if (auto time = std::chrono::system_clock::now(); time - time_last >= std::chrono::seconds(1)) {
			switch (prompt()) {
				case PromptResult::Confirmed: result = run_update() ? 0 : 1; goto END;
				case PromptResult::Cancelled: goto END;
				default: /* Fall through if pending. */ break;
			}
			time_last = time;
		}
	}
END:
	{
		// Reset terminal and file descriptor settings
		fcntl(STDIN_FILENO, F_SETFL, stdin_flags);
		tcsetattr(STDIN_FILENO, TCSANOW, &old_settings);
		return result;
	}
}
