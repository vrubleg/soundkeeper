using System;
using System.Collections.Generic;
using System.Text;
using System.Text.RegularExpressions;
using System.Linq;
using System.Diagnostics;
using System.Globalization;

var args = Environment.GetCommandLineArgs().ToList();
args = (args.IndexOf("--") > -1) ? args.Skip(args.IndexOf("--") + 1).ToList() : (new List<string>());

if (args.Count == 0 || args[0][0] == '-')
{
	Console.WriteLine("Error: Target file name is not specified.");
	Console.WriteLine("Usage: csi BuildInfo.csx -- <BuildInfo.hpp> [--no-errors] [--force]");
	Environment.Exit(1);
}

var BUILD_INFO_FILE = args[0];
var NO_ERRORS = args.IndexOf("--no-errors") != -1;
var FORCE_UPDATE = args.IndexOf("--force") != -1;

class Error : Exception
{
	public Error() { }
	public Error(string message) : base(message) { }
	public Error(string message, Exception e) : base(message, e) { }
}

class Success : Exception
{
	public Success() { }
	public Success(string message) : base(message) { }
	public Success(string message, Exception e) : base(message, e) { }
}

int RunCmd(string cmd, out string stdout, out string stderr)
{
	string name = "";
	string args = "";

	cmd = cmd.Trim();
	if (cmd.Length == 0) { throw new ArgumentException("Empty command"); }

	if (cmd[0] == '"')
	{
		var split_pos = cmd.IndexOf('"', 1);
		if (split_pos == -1) { throw new ArgumentException("Unpaired quote in command"); }
		name = cmd.Substring(1, split_pos).Trim();
		args = cmd.Substring(split_pos + 1).Trim();
	}
	else
	{
		var split_pos = cmd.IndexOfAny(new char[] {' ', '\t'});
		if (split_pos == -1)
		{
			name = cmd;
		}
		else
		{
			name = cmd.Substring(0, split_pos);
			args = cmd.Substring(split_pos + 1).Trim();
		}
	}

	using var process = new Process();
	var stdout_buffer = new StringBuilder();
	var stderr_buffer = new StringBuilder();

	process.StartInfo.FileName = name;
	process.StartInfo.Arguments = args;
	process.StartInfo.UseShellExecute = false;
	process.StartInfo.RedirectStandardOutput = true;
	process.StartInfo.RedirectStandardError = true;
	process.OutputDataReceived += new DataReceivedEventHandler((sender, e) => { stdout_buffer.AppendLine(e.Data); });
	process.ErrorDataReceived += new DataReceivedEventHandler((sender, e) => { stderr_buffer.AppendLine(e.Data); });

	process.Start();
	process.BeginOutputReadLine();
	process.BeginErrorReadLine();
	process.WaitForExit();

	stdout = stdout_buffer.ToString();
	stderr = stderr_buffer.ToString();
	return process.ExitCode;
}

int RunCmd(string cmd, out string stdout)
{
	return RunCmd(cmd, out stdout, out var stderr);
}

int RunCmd(string cmd)
{
	return RunCmd(cmd, out var stdout, out var stderr);
}

class DefineItem
{
	public string Prefix  = "";
	public string Name    = "";
	public string Space   = "";
	public string Value   = "";
	public string Ignored = "";
	public bool   Deleted = false;

	public bool IsDefine { get { return Name != ""; } }

	public DefineItem(string line)
	{
		Parse(line);
	}

	public DefineItem(string name, string value)
	{
		Prefix  = "#define ";
		Name    = name;
		Space   = " ";
		Value   = value;
		Ignored = "";
		Deleted = false;
	}

	public void Parse(string line)
	{
		var match = Regex.Match(line, @"^(\s*#define\s+)([A-Za-z0-9_]+)(?:(\s+)(.*))?$");

		if (match.Success)
		{
			Prefix  = match.Groups[1].Value;
			Name    = match.Groups[2].Value;
			Space   = match.Groups[3].Value;
			Value   = match.Groups[4].Value;
			Ignored = "";
		}
		else
		{
			Prefix  = "";
			Name    = "";
			Space   = "";
			Value   = "";
			Ignored = line;
		}

		Deleted = false;
	}

	public string ToString()
	{
		return Prefix + Name + (Name != "" && Space == "" && Value != "" ? " " : Space) + Value + Ignored;
	}
}

class DefineEditor : List<DefineItem>
{
	public string FileName { get; set; }
	public Encoding Encoding { get; protected set; }

	private bool IsUTF8(byte[] data)
	{
		try
		{
			new UTF8Encoding(false, true).GetCharCount(data);
			return true;
		}
		catch
		{
			return false;
		}
	}

	public DefineEditor(string filename = null) : base()
	{
		Load(filename);
	}

	public bool Load()
	{
		return Load(FileName);
	}

	bool Load(string filename)
	{
		Clear();

		FileName = filename;
		Encoding = new UTF8Encoding(false);

		if (filename == null || !File.Exists(filename))
		{
			return false;
		}

		byte[] FileData = File.ReadAllBytes(filename);
		Encoding = (IsUTF8(FileData)) ? new UTF8Encoding(false) : Encoding.Default;
		var DataReader = new StreamReader(new MemoryStream(FileData), Encoding);
		string line;
		while ((line = DataReader.ReadLine()) != null)
		{
			this.Add(new DefineItem(line));
		}

		return true;
	}

	public void Save()
	{
		Save(FileName);
	}

	public void Save(string filename)
	{
		var DataStream = new FileStream(filename, FileMode.Create, FileAccess.Write);
		var DataWriter = new StreamWriter(DataStream, Encoding);
		foreach (DefineItem item in this)
		{
			if (item.Deleted) { break; }
			DataWriter.WriteLine(item.ToString());
		}
		DataWriter.Close();
	}

	public string GetRaw(string name)
	{
		return this.LastOrDefault(item => !item.Deleted && item.Name == name)?.Value;
	}

	public string GetRaw(string name, string defval)
	{
		return GetRaw(name) ?? defval;
	}

	public bool UpdRaw(string name, string value)
	{
		var item = this.LastOrDefault(item => item.Name == name);
		if (item == null)
		{
			return false;
		}
		else
		{
			item.Value = value;
			item.Deleted = false;
			return true;
		}
	}

	public void SetRaw(string name, string value)
	{
		if (!this.UpdRaw(name, value))
		{
			this.Add(new DefineItem(name, value));
		}
	}

	public void Delete(string name)
	{
		this.FindAll(item => item.Name == name).ForEach(item => item.Deleted = true);
	}

	public int GetInt(string name, int defval)
	{
		var value = (GetRaw(name) ?? "").Trim();
		if (value == "") { return defval; }
		if (value.StartsWith("0x"))
		{
			return Int32.Parse(value.Substring(2), NumberStyles.AllowHexSpecifier);
		}
		else
		{
			return Int32.Parse(value);
		}
	}

	public void SetInt(string name, int value, bool hex = false)
	{
		SetRaw(name, hex ? ("0x" + value.ToString("X8")) : value.ToString());
	}

	public void UpdInt(string name, int value, bool hex = false)
	{
		UpdRaw(name, hex ? ("0x" + value.ToString("X8")) : value.ToString());
	}
}

try
{
	string output;

	if (RunCmd("where git") != 0)
	{
		throw new Error("Git not found");
	}

	if (FORCE_UPDATE)
	{
		Console.WriteLine("Updating build information...");
	}
	else
	{
		Console.WriteLine("Checking if there are any changes...");

		// Check if there are any changes in the working tree since last commit.

		int exitcode = RunCmd("git diff HEAD --exit-code --quiet");

		if (exitcode == 0)
		{
			throw new Success("No changes found");
		}

		if (exitcode != 1)
		{
			throw new Error("An issue with Git repo");
		}

		// Check if there are any changes since last build info update.

		if (RunCmd("git ls-files -z --cached --modified --other --exclude-standard --deduplicate", out output) != 0)
		{
			throw new Error("Can't list files in the repo");
		}

		var files = output.Split(new char[] {'\0'}, StringSplitOptions.RemoveEmptyEntries);
		var latest_change = DateTime.MinValue;

		foreach (var file in files)
		{
			if (!File.Exists(file)) { continue; }
			var LastWriteTime = new FileInfo(file).LastWriteTime;
			if (LastWriteTime > latest_change) { latest_change = LastWriteTime; }
		}

		var latest_update = File.Exists(BUILD_INFO_FILE) ? new FileInfo(BUILD_INFO_FILE).LastWriteTime : DateTime.MinValue;
		if (latest_update >= latest_change)
		{
			throw new Success("No new changes found");
		}

		Console.WriteLine("There are some changes. Updating build information...");
	}

	var build_info = new DefineEditor(BUILD_INFO_FILE);

	// Get expected version from Git tags.

	int rev_major = 0;
	int rev_minor = 0;
	int rev_patch = 0;
	int rev_extra = 0;

	if (RunCmd("git describe --tags --match \"v[0-9]*.[0-9]*.[0-9]*\" --abbrev=8 HEAD", out output) == 0)
	{
		// Get version from the latest revision tag.
		var match = Regex.Match(output.Trim().ToLower(), @"^v([0-9]+)\.([0-9]+)\.([0-9]+)(?:-([0-9]+)-g([a-f0-9]+))?$");
		if (match.Success)
		{
			rev_major = Int32.Parse(match.Groups[1].Value);
			rev_minor = Int32.Parse(match.Groups[2].Value);
			rev_patch = Int32.Parse(match.Groups[3].Value);
			rev_extra = match.Groups[4].Value != "" ? Int32.Parse(match.Groups[4].Value) : 0;
		}
		else
		{
			throw new Error("Cannot parse Git version tag");
		}
	}
	else
	{
		// No version tag found. Get just revision count.
		if (RunCmd("git rev-list --count HEAD", out output) == 0)
		{
			output = output.Trim();
			rev_extra = output != "" ? Int32.Parse(output) : 0;
		}
		else
		{
			throw new Error("Cannot get revision count from Git");
		}
	}

	rev_extra++;

	// Update version.

	var want_rev = new Version(rev_major, rev_minor, rev_patch, rev_extra);
	var curr_rev = new Version(build_info.GetInt("REV_MAJOR", 0), build_info.GetInt("REV_MINOR", 0), build_info.GetInt("REV_PATCH", 0), build_info.GetInt("REV_EXTRA", 0));

	if (want_rev > curr_rev)
	{
		build_info.SetInt("REV_MAJOR", rev_major);
		build_info.SetInt("REV_MINOR", rev_minor);
		build_info.SetInt("REV_PATCH", rev_patch);
		build_info.SetInt("REV_EXTRA", rev_extra);
		build_info.UpdInt("REV_BUILD", 0);
	}

	// Update version build counter.

	var rev_build = build_info.GetInt("REV_BUILD", 0);
	rev_build++;
	build_info.UpdInt("REV_BUILD", rev_build);

	// Update date.

	var rev_time = DateTime.Now;

	if (build_info.GetInt("REV_YEAR", 0) != rev_time.Year
		|| build_info.GetInt("REV_MONTH", 0) != rev_time.Month
		|| build_info.GetInt("REV_DAY", 0) != rev_time.Day)
	{
		build_info.SetInt("REV_YEAR", rev_time.Year);
		build_info.SetInt("REV_MONTH", rev_time.Month);
		build_info.SetInt("REV_DAY", rev_time.Day);
		build_info.UpdInt("REV_DAY_BUILD", 0);
	}

	// Update date build counter.

	var rev_day_build = build_info.GetInt("REV_DAY_BUILD", 0);
	rev_day_build++;
	build_info.UpdInt("REV_DAY_BUILD", rev_day_build);

	// Update time.

	build_info.UpdInt("REV_HOUR",      rev_time.Hour);
	build_info.UpdInt("REV_MINUTE",    rev_time.Minute);
	build_info.UpdInt("REV_SECOND",    rev_time.Second);
	build_info.UpdInt("REV_TIMESTAMP", (int) new DateTimeOffset(rev_time).ToUnixTimeSeconds());

	build_info.Save();
}
catch (Success ex)
{
	Console.WriteLine(ex.Message + ".");
	Environment.Exit(0);
}
catch (Error ex)
{
	if (NO_ERRORS)
	{
		Console.WriteLine($"Warning: {ex.Message}.");
		Environment.Exit(0);
	}
	else
	{
		Console.WriteLine($"Error: {ex.Message}.");
		Environment.Exit(1);
	}
}
catch (Exception ex)
{
	if (NO_ERRORS)
	{
		Console.WriteLine($"Warning {ex.GetType().Name}: {ex.Message}.");
		Environment.Exit(0);
	}
	else
	{
		Console.WriteLine($"Error {ex.GetType().Name}: {ex.Message}.");
		Environment.Exit(1);
	}
}
