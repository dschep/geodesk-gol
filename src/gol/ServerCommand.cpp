// Copyright (c) 2025 Clarisma / GeoDesk contributors
// SPDX-License-Identifier: AGPL-3.0-only

#include "ServerCommand.h"
#include "gol/query/QuerySpec.h"
#include "gol/query/BriefQueryPrinter.h"
#include "gol/query/CountQueryPrinter.h"
#include "gol/query/CsvQueryPrinter.h"
#include "gol/query/GeoJsonQueryPrinter.h"
#include "gol/query/ListQueryPrinter.h"
#include "gol/query/WktQueryPrinter.h"
#include "gol/query/XmlQueryPrinter.h"
#include "gol/util/BoxParser.h"
#include "gol/util/PolygonParser.h"
#include <clarisma/cli/Console.h>
#include <httplib.h>
#include <cstdio>
#include <mutex>
#include <iostream>

using namespace clarisma;
using namespace geodesk;

ServerCommand::ServerCommand()
{
    Option options[] = {
        { "port", OPTION_METHOD(&ServerCommand::setPort) },
        { "p", OPTION_METHOD(&ServerCommand::setPort) },
        { "cors", OPTION_METHOD(&ServerCommand::setCors) }
    };
    addOptions(options, 3);
}

int ServerCommand::setPort(std::string_view value)
{
    try {
        port_ = std::stoi(std::string(value));
    } catch (...) {
        return 0; 
    }
    return 1;
}

int ServerCommand::setCors(std::string_view value)
{
    corsAllowedOrigins_ = value;
    return 1;
}

OutputFormat ServerCommand::parseFormat(std::string_view s)
{
    if (s.empty()) return OutputFormat::GEOJSON;
    if (s == "json" || s == "geojson") return OutputFormat::GEOJSON;
    if (s == "jsonl" || s == "geojsonl" || s == "ndjson") return OutputFormat::GEOJSONL;
    if (s == "brief") return OutputFormat::BRIEF;
    if (s == "count") return OutputFormat::COUNT;
    if (s == "csv") return OutputFormat::CSV;
    if (s == "list") return OutputFormat::LIST;
    if (s == "wkt") return OutputFormat::WKT;
    if (s == "xml") return OutputFormat::XML;
    return OutputFormat::UNKNOWN;
}

int ServerCommand::run(char* argv[])
{
    // Skip opening store in GolCommand::run by using DO_NOT_OPEN if we wanted
    // but GolCommand::run opens it if openMode_ is not DO_NOT_OPEN.
    // Default is READ. That is what we want.
    if (GolCommand::run(argv) != 0) return 1;

    httplib::Server svr;
    std::mutex mutex;

    svr.Get("/", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mutex);

        res.set_header("Access-Control-Allow-Origin", corsAllowedOrigins_);
        res.set_header("Access-Control-Allow-Methods", "GET");

        std::string query = req.get_param_value("query");
        std::string formatStr = req.get_param_value("format");
        std::string bbox = req.get_param_value("bbox");
        std::string area = req.get_param_value("area");

        OutputFormat format = parseFormat(formatStr);
        if (format == OutputFormat::UNKNOWN) {
            res.status = 400;
            res.set_content("Invalid format", "text/plain");
            return;
        }

        FILE* tempOut = std::tmpfile();
        if(!tempOut) {
            res.status = 500;
            res.set_content("Internal Server Error", "text/plain");
            return;
        }
        int fd = fileno(tempOut);
        
        // Save stdout? We assume stdout is fd 1.
        // We set Console output to temp file.
        Console::setOutputFile(fd);
        
        try {
            Box bounds = Box::ofWorld();
            std::unique_ptr<const Filter> filter;
            
            if(!bbox.empty()) {
                bounds = BoxParser(bbox.c_str()).parse();
            }
            if(!area.empty()) {
                PolygonParser parser(area.c_str());
                filter = parser.parse();
                bounds = filter->getBounds();
            }

            const MatcherHolder* matcher = store_.getMatcher(query.c_str());
            // precision 7, no keys filter
            std::string keys = "";
            if (format == OutputFormat::CSV) keys = "id,lon,lat,tags";
            QuerySpec spec(&store_, bounds, matcher->acceptedTypes(),
                matcher, filter.get(), 7, keys); 

            switch (format)
            {
            case OutputFormat::BRIEF: BriefQueryPrinter(&spec).run(); break;
            case OutputFormat::COUNT: CountQueryPrinter(&spec).run(); break;
            case OutputFormat::CSV: CsvQueryPrinter(&spec).run(); break;
            case OutputFormat::GEOJSON: GeoJsonQueryPrinter(&spec, false).run(); break;
            case OutputFormat::GEOJSONL: GeoJsonQueryPrinter(&spec, true).run(); break;
            case OutputFormat::LIST: ListQueryPrinter(&spec).run(); break;
            case OutputFormat::WKT: WktQueryPrinter(&spec).run(); break;
            case OutputFormat::XML: XmlQueryPrinter(&spec).run(); break;
            default: break;
            }
        } catch (const std::exception& ex) {
            Console::setOutputFile(1); // Restore stdout
            res.status = 400;
            res.set_content(ex.what(), "text/plain");
            fclose(tempOut);
            return;
        } catch (...) {
            Console::setOutputFile(1); // Restore stdout
            res.status = 500;
            res.set_content("Unknown error", "text/plain");
            fclose(tempOut);
            return;
        }

        Console::setOutputFile(1); // Restore stdout

        fseek(tempOut, 0, SEEK_END);
        long size = ftell(tempOut);
        rewind(tempOut);
        std::string body;
        body.resize(size);
        if (size > 0) {
            fread(&body[0], 1, size, tempOut);
        }
        fclose(tempOut);

        std::string contentType = "text/plain";
        if (format == OutputFormat::GEOJSON || format == OutputFormat::GEOJSONL) contentType = "application/json";
        else if (format == OutputFormat::CSV) contentType = "text/csv";
        else if (format == OutputFormat::XML) contentType = "application/xml";

        res.set_content(body, contentType);
    });

    std::cout << "Listening on port " << port_ << "..." << std::endl;
    svr.listen("0.0.0.0", port_);

    return 0;
}
