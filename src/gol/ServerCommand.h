// Copyright (c) 2025 Clarisma / GeoDesk contributors
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once
#include "GolCommand.h"
#include "gol/query/OutputFormat.h"

class ServerCommand : public GolCommand
{
public:
    ServerCommand();
    int run(char* argv[]) override;

private:
    int port_ = 8000;
    std::string corsAllowedOrigins_ = "*";

    int setPort(std::string_view value);
    int setCors(std::string_view value);
    static OutputFormat parseFormat(std::string_view s);
};
