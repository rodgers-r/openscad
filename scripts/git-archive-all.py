#! /usr/bin/env python3
# coding=utf-8

from __future__ import print_function
from __future__ import unicode_literals

__version__ = "1.7"

import sys
from os import path, extsep
from subprocess import Popen, PIPE, CalledProcessError


class GitArchiver(object):
    """
    GitArchiver

    Scan a git repository and export all tracked files, and submodules.
    Checks for .gitattributes files in each directory and uses 'export-ignore'
    pattern entries for ignore files in the archive.

    Automatically detects output format extension: zip, tar, bz2, or gz.
    """

    def __init__(self, prefix='', verbose=False, exclude=True, force_sub=False, extra=None, main_repo_abspath=None):
        """
        @type prefix:   string
        @param prefix:  Prefix used to prepend all paths in the resulting archive.

        @type verbose:  bool
        @param verbose: Determines verbosity of the output (stdout).

        @type exclude:  bool
        @param exclude: Determines whether archiver should follow rules specified in .gitattributes files.
                        Defaults to True.

        @type force_sub:    bool
        @param force_sub:   Determines whether submodules are initialized and updated before archiving.
                            Defaults to False

        @type extra:    list
        @param extra:   List of extra paths to include in the resulting archive.

        @type main_repo_abspath:    string
        @param main_repo_abspath:   Absolute path to the main repository (or one of subdirectories).
                                    If None, current cwd is used.
                                    If given path is path to a subdirectory (but not a submodule directory!)
                                    it will be replaced with abspath to toplevel directory of the repository.
        """
        if extra is None:
            extra = []

        if main_repo_abspath is None:
            main_repo_abspath = path.abspath('')
        elif not path.isabs(main_repo_abspath):
            raise ValueError("You MUST pass absolute path to the main git repository.")

        # Raises an exception if there is no repo under main_repo_abspath.
        try:
            self.run_shell("[ -d .git ] || git rev-parse --git-dir > /dev/null 2>&1", main_repo_abspath)
        except Exception as e:
            raise ValueError("Not a git repository (or any of the parent directories).".format(path=main_repo_abspath))

        # Detect toplevel directory of the repo.
        main_repo_abspath = path.abspath(self.read_git_shell('git rev-parse --show-toplevel', main_repo_abspath).rstrip())

        self.prefix = prefix
        self.verbose = verbose
        self.exclude = exclude
        self.extra = extra
        self.force_sub = force_sub
        self.main_repo_abspath = main_repo_abspath

    def create(self, output_path, dry_run=False, output_format=None):
        """
        Creates the archive, written to the given output_file_path

        Type of the archive is determined either by extension of output_file_path or by the format argument.
        Supported formats are: gz, zip, bz2, tar, tgz

        @type output_path:     string
        @param output_path:    Output file path.

        @type dry_run:  bool
        @param dry_run: Determines whether create should do nothing but print what it would archive.

        @type output_format:    string
        @param output_format:   Determines format of the output archive.
                                If None, format is determined from extension of output_file_path.
        """
        if output_format is None:
            file_name, file_ext = path.splitext(output_path)
            output_format = file_ext[len(extsep):].lower()

        if output_format == 'zip':
            from zipfile import ZipFile, ZIP_DEFLATED

            if not dry_run:
                archive = ZipFile(path.abspath(output_path), 'w')
                add = lambda file_path, file_name: archive.write(file_path, path.join(self.prefix, file_name), ZIP_DEFLATED)
        elif output_format in ['tar', 'bz2', 'gz', 'tgz']:
            import tarfile

            if output_format == 'tar':
                t_mode = 'w'
            elif output_format == 'tgz':
                t_mode = 'w:gz'
            else:
                t_mode = 'w:{f}'.format(f=output_format)

            if not dry_run:
                archive = tarfile.open(path.abspath(output_path), t_mode)
                add = lambda file_path, file_name: archive.add(file_path, path.join(self.prefix, file_name))
        else:
            raise RuntimeError("Unknown format: {f}".format(f=output_format))

        for file_path in self.extra:
            if not dry_run:
                if self.verbose:
                    print("Compressing {f} => {a}...".format(f=file_path,
                                                             a=path.join(self.prefix, file_path)))
                add(file_path, file_path)
            else:
                print("{f} => {a}".format(f=file_path,
                                          a=path.join(self.prefix, file_path)))

        for file_path in self.list_files():
            if not dry_run:
                if self.verbose:
                    print("Compressing {f} => {a}...".format(f=path.join(self.main_repo_abspath, file_path),
                                                             a=path.join(self.prefix, file_path)))
                add(path.join(self.main_repo_abspath, file_path), file_path)
            else:
                print("{f} => {a}".format(f=path.join(self.main_repo_abspath, file_path),
                                          a=path.join(self.prefix, file_path)))

        if not dry_run:
            archive.close()

    def get_path_components(self, repo_abspath, abspath):
        """
        Splits given abspath into components until repo_abspath is reached.

        E.g. if repo_abspath is '/Documents/Hobby/ParaView/' and abspath is
        '/Documents/Hobby/ParaView/Catalyst/Editions/Base/', function will return:
        ['.', 'Catalyst', 'Editions', 'Base']

        First element is always '.' (concrete symbol depends on OS).

        @type repo_abspath:     string
        @param repo_abspath:    Absolute path to the git repository.

        @type abspath:  string
        @param abspath: Absolute path to within repo_abspath.

        @rtype:     list
        @return:    List of path components.
        """
        components = []

        while not path.samefile(abspath, repo_abspath):
            abspath, tail = path.split(abspath)

            if len(tail):
                components.insert(0, tail)

        components.insert(0, path.relpath(repo_abspath, repo_abspath))
        return components

    def get_exclude_patterns(self, repo_abspath, repo_file_paths):
        """
        Returns exclude patterns for a given repo. It looks for .gitattributes files in repo_file_paths.

        Resulting dictionary will contain exclude patterns per path (relative to the repo_abspath).
        E.g. {('.', 'Catalyst', 'Editions', 'Base'), ['Foo*', '*Bar']}

        @type repo_abspath:     string
        @param repo_abspath:    Absolute path to the git repository.

        @type repo_file_paths:  list
        @param repo_file_paths: List of paths relative to the repo_abspath that are under git control.

        @rtype:         dict
        @return:    Dictionary representing exclude patterns.
                    Keys are tuples of strings. Values are lists of strings.
                    Returns None if self.exclude is not set.
        """
        if not self.exclude:
            return None

        def read_attributes(attributes_abspath):
            patterns = []
            if path.isfile(attributes_abspath):
                attributes = open(attributes_abspath, 'r').readlines()
                patterns = []
                for line in attributes:
                    tokens = line.strip().split()
                    if "export-ignore" in tokens[1:]:
                        patterns.append(tokens[0])
            return patterns

        exclude_patterns = {(): []}

        # There may be no gitattributes.
        try:
            global_attributes_abspath = self.read_shell("git config --get core.attributesfile", repo_abspath).rstrip()
            exclude_patterns[()] = read_attributes(global_attributes_abspath)
        except Exception:
            # And valid to not have them.
            pass

        for attributes_abspath in [path.join(repo_abspath, f) for f in repo_file_paths if f.endswith(".gitattributes")]:
            # Each .gitattributes affects only files within its directory.
            key = tuple(self.get_path_components(repo_abspath, path.dirname(attributes_abspath)))
            exclude_patterns[key] = read_attributes(attributes_abspath)

        local_attributes_abspath = path.join(repo_abspath, ".git", "info", "attributes")
        key = tuple(self.get_path_components(repo_abspath, repo_abspath))

        if key in exclude_patterns:
            exclude_patterns[key].extend(read_attributes(local_attributes_abspath))
        else:
            exclude_patterns[key] = read_attributes(local_attributes_abspath)

        return exclude_patterns

    def is_file_excluded(self, repo_abspath, repo_file_path, exclude_patterns):
        """
        Checks whether file at a given path is excluded.

        @type repo_abspath: string
        @param repo_abspath: Absolute path to the git repository.

        @type repo_file_path:   string
        @param repo_file_path:  Path to a file within repo_abspath.

        @type exclude_patterns:     dict
        @param exclude_patterns:    Exclude patterns with format specified for get_exclude_patterns.

        @rtype: bool
        @return: True if file should be excluded. Otherwise False.
        """
        if exclude_patterns is None or not len(exclude_patterns):
            return False

        from fnmatch import fnmatch

        file_name = path.basename(repo_file_path)
        components = self.get_path_components(repo_abspath, path.join(repo_abspath, path.dirname(repo_file_path)))

        is_excluded = False
        # We should check all patterns specified in intermediate directories to the given file.
        # At the end we should also check for the global patterns (key '()' or empty tuple).
        while not is_excluded:
            key = tuple(components)
            if key in exclude_patterns:
                patterns = exclude_patterns[key]
                for p in patterns:
                    if fnmatch(file_name, p) or fnmatch(repo_file_path, p):
                        if self.verbose:
                            print("Exclude pattern matched {pattern}: {path}".format(pattern=p, path=repo_file_path))
                        is_excluded = True

            if not len(components):
                break

            components.pop()

        return is_excluded

    def list_files(self, repo_path=''):
        """
        An iterator method that yields a file path relative to main_repo_abspath
        for each file that should be included in the archive.
        Skips those that match the exclusion patterns found in
        any discovered .gitattributes files along the way.

        Recurs into submodules as well.

        @type repo_path:    string
        @param repo_path:   Path to the git submodule repository within the main git repository.

        @rtype:     iterator
        @return:    Iterator to traverse files under git control relative to main_repo_abspath.
        """
        repo_abspath = path.join(self.main_repo_abspath, repo_path)
        repo_file_paths = self.read_git_shell("git ls-files --cached --full-name --no-empty-directory", repo_abspath).splitlines()
        exclude_patterns = self.get_exclude_patterns(repo_abspath, repo_file_paths)

        for repo_file_path in repo_file_paths:
            # Git puts path in quotes if file path has unicode characters.
            repo_file_path = repo_file_path.strip('"')  # file path relative to current repo
            file_name = path.basename(repo_file_path)

            # Only list symlinks and files that don't start with git.
            if file_name.startswith(".git") or (not path.islink(repo_file_path) and path.isdir(repo_file_path)):
                continue

            main_repo_file_path = path.join(repo_path, repo_file_path)  # file path relative to the main repo

            if self.is_file_excluded(repo_abspath, repo_file_path, exclude_patterns):
                continue

            # Yield both repo_file_path and main_repo_file_path to preserve structure of the repo.
            yield main_repo_file_path

        if self.force_sub:
            self.run_shell("git submodule init", repo_abspath)
            self.run_shell("git submodule update", repo_abspath)

        # List files of every submodule.
        for submodule_path in self.read_shell("git submodule --quiet foreach 'pwd'", repo_abspath).splitlines():
            # In order to get output path we need to exclude repository path from submodule_path.
            submodule_path = path.relpath(submodule_path, self.main_repo_abspath)
            for file_path in self.list_files(submodule_path):
                yield file_path

    @staticmethod
    def run_shell(cmd, cwd=None):
        """
        Runs shell command.

        @type cmd:  string
        @param cmd: Command to be executed.

        @type cwd:  string
        @param cwd: Working directory.

        @rtype:     int
        @return:    Return code of the command.

        @raise CalledProcessError:  Raises exception if return code of the command is non-zero.
        """
        p = Popen(cmd, shell=True, cwd=cwd)
        p.wait()

        if p.returncode:
            raise CalledProcessError(returncode=p.returncode, cmd=cmd)

        return p.returncode

    @staticmethod
    def read_shell(cmd, cwd=None, encoding='utf-8'):
        """
        Runs shell command and reads output.

        @type cmd:  string
        @param cmd: Command to be executed.

        @type cwd:  string
        @param cwd: Working directory.

        @type encoding: string
        @param encoding: Encoding used to decode bytes returned by Popen into string.

        @rtype:     string
        @return:    Output of the command.

        @raise CalledProcessError:  Raises exception if return code of the command is non-zero.
        """
        p = Popen(cmd, shell=True, stdout=PIPE, cwd=cwd)
        output, _ = p.communicate()
        output = output.decode(encoding)

        if p.returncode:
            raise CalledProcessError(returncode=p.returncode, cmd=cmd, output=output)

        return output

    @staticmethod
    def read_git_shell(cmd, cwd=None):
        """
        Runs git shell command, reads output and decodes it into unicode string

        @type cmd:  string
        @param cmd: Command to be executed.

        @type cwd:  string
        @param cwd: Working directory.

        @rtype:     string
        @return:    Output of the command.

        @raise CalledProcessError:  Raises exception if return code of the command is non-zero.
        """
        p = Popen(cmd, shell=True, stdout=PIPE, cwd=cwd)
        output, _ = p.communicate()
        output = output.decode('unicode_escape').encode('raw_unicode_escape').decode('utf-8')

        if p.returncode:
            raise CalledProcessError(returncode=p.returncode, cmd=cmd, output=output)

        return output


if __name__ == '__main__':
    from optparse import OptionParser

    parser = OptionParser(usage="usage: %prog [-v] [--prefix PREFIX] [--no-exclude] [--force-submodules] [--dry-run] OUTPUT_FILE",
                          version="%prog {version}".format(version=__version__))

    parser.add_option('--prefix',
                      type='string',
                      dest='prefix',
                      default='',
                      help="Prepend PREFIX to each filename in the archive. OUTPUT_FILE name is used by default to avoid tarbomb.")

    parser.add_option('-v', '--verbose',
                      action='store_true',
                      dest='verbose',
                      help='Enable verbose mode.')

    parser.add_option('--no-exclude',
                      action='store_false',
                      dest='exclude',
                      default=True,
                      help="Don't read .gitattributes files for patterns containing export-ignore attrib.")

    parser.add_option('--force-submodules',
                      action='store_true',
                      dest='force_sub',
                      help="Force a git submodule init && git submodule update at each level before iterating submodules.")

    parser.add_option('--extra',
                      action='append',
                      dest='extra',
                      default=[],
                      help="Any additional files to include in the archive.")
    parser.add_option('--dry-run',
                      action='store_true',
                      dest='dry_run',
                      help="Don't actually archive anything, just show what would be done.")

    options, args = parser.parse_args()

    if len(args) != 1:
        parser.error("You must specify exactly one output file")

    output_file_path = args[0]

    if path.isdir(output_file_path):
        parser.error("You cannot use directory as output")

    # avoid tarbomb
    if options.prefix:
        options.prefix = path.join(options.prefix, '')
    else:
        import re

        output_name = path.basename(output_file_path)
        output_name = re.sub('(\.zip|\.tar|\.tgz|\.gz|\.bz2|\.tar\.gz|\.tar\.bz2)$', '', output_name) or "Archive"
        options.prefix = path.join(output_name, '')

    try:
        archiver = GitArchiver(options.prefix,
                               options.verbose,
                               options.exclude,
                               options.force_sub,
                               options.extra)
        archiver.create(output_file_path, options.dry_run)
    except Exception as e:
        parser.exit(2, "{exception}\n".format(exception=e))

    sys.exit(0)
