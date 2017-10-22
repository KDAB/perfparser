import qbs

Project {
    name: "PerfParserAutotests"
    condition: project.withAutotests
    references: [
        "elfmap", "kallsyms", "perfdata", "addresscache"
    ]
}
