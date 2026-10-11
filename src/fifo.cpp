/**
 * Copyright (c) 2014 - 2016 Tolga Cakir <tolga@cevel.net>
 *
 * This source file is part of Sidewinder daemon and is distributed under the
 * MIT License. For more information, see LICENSE file.
 */

#include <csignal>
#include <cerrno>
#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <unistd.h>

#include <sys/stat.h>

#include <fifo.hpp>

int Fifo::enable(std::string output) {
	output_ = output;

	// ignore SIGPIPE, else program terminates on unsuccessful write()
	//Las llaves son imprescindibles: sin ellas sa_mask y sa_flags quedaban
	//indefinidos y se pasaban a sigaction(). process.cpp lo hace asi y lo
	//deja comentado explicitamente; aqui faltaba.
	struct sigaction ignore{};
	ignore.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &ignore, nullptr);

	if (mkfifo(output_.c_str(), 0644)) {
		std::cerr << "Error creating FIFO." << std::endl;

		return 1;
	}

	std::cerr << "FIFO: " << output << " has been created." << std::endl
		  << "Waiting for user to open it." << std::endl;
	fd_ = open(output_.c_str(), O_WRONLY);

	if (fd_ < 0) {
		if (errno != EINTR) { //EINTR means stopped by abort signal
			std::cerr << "Can't open FIFO for writing." << std::endl;
		}
		return 1;
	}
	return 0;
}

void Fifo::disable() {
	if (fd_) {
		close(fd_);
		fd_ = -1;
		unlink(output_.c_str());
	}
}

void Fifo::output(std::vector<unsigned char> *buffer) {
	if (fd_ == -1) {
		return;
	}

	const ssize_t written = write(fd_, buffer->data(), buffer->size());
	if (written < 0) {
		std::cerr << "Write to FIFO failed: " << std::strerror(errno) << std::endl;
	} else if (static_cast<size_t>(written) != buffer->size()) {
		//Una escritura parcial son bytes de transport stream perdidos.
		std::cerr << "Short write to FIFO: " << written << " of "
			  << buffer->size() << " bytes." << std::endl;
	}
}

Fifo::Fifo() {
	fd_ = -1;
}

Fifo::~Fifo() {
	disable();
}
