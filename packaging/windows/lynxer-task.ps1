<#
.SYNOPSIS
Install, remove, start or inspect a Lynxer program as a Windows scheduled task.

.DESCRIPTION
This is the Windows counterpart of the Linux `server` unit story. It keeps the
program as a foreground console process, but lets Task Scheduler start it at
logon (default) or at boot (requires elevation).

.EXAMPLE
.\lynxer-task.ps1 install -Script C:\apps\server.lynx -AtLogOn

.EXAMPLE
.\lynxer-task.ps1 status

.EXAMPLE
.\lynxer-task.ps1 uninstall
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('install', 'uninstall', 'run', 'status')]
    [string]$Action,

    [string]$Name = 'Lynxer',

    [string]$Script,

    [string[]]$Arguments = @(),

    [string]$Lynxer,

    [switch]$AtStartup
)

$ErrorActionPreference = 'Stop'

function Resolve-Lynxer {
    if ($Lynxer) {
        if (-not (Test-Path -LiteralPath $Lynxer -PathType Leaf)) {
            throw "Lynxer executable not found: $Lynxer"
        }
        return (Resolve-Path -LiteralPath $Lynxer).Path
    }
    $command = Get-Command lynxer.exe -ErrorAction SilentlyContinue
    if (-not $command) {
        $command = Get-Command lynxer -ErrorAction SilentlyContinue
    }
    if (-not $command) {
        throw 'lynxer was not found on PATH; pass -Lynxer <path>.'
    }
    return $command.Source
}

switch ($Action) {
    'install' {
        if (-not $Script) {
            throw 'install requires -Script <file.lynx>.'
        }
        if (-not (Test-Path -LiteralPath $Script -PathType Leaf)) {
            throw "Lynxer program not found: $Script"
        }
        $exe = Resolve-Lynxer
        $scriptPath = (Resolve-Path -LiteralPath $Script).Path
        $quoted = @("`"$scriptPath`"") + $Arguments
        $action = New-ScheduledTaskAction -Execute $exe -Argument ($quoted -join ' ')
        $trigger = if ($AtStartup) {
            New-ScheduledTaskTrigger -AtStartup
        } else {
            New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
        }
        $principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Limited
        $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit ([TimeSpan]::Zero)
        Register-ScheduledTask -TaskName $Name -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Description "Lynxer program: $scriptPath" -Force | Out-Null
        Write-Host "Installed scheduled task '$Name' for $scriptPath"
        if ($AtStartup) {
            Write-Host 'AtStartup tasks may require running this script as Administrator.'
        }
    }
    'uninstall' {
        $task = Get-ScheduledTask -TaskName $Name -ErrorAction SilentlyContinue
        if (-not $task) {
            Write-Host "Scheduled task '$Name' is not installed."
            return
        }
        Unregister-ScheduledTask -TaskName $Name -Confirm:$false
        Write-Host "Removed scheduled task '$Name'."
    }
    'run' {
        Start-ScheduledTask -TaskName $Name
        Write-Host "Started scheduled task '$Name'."
    }
    'status' {
        $task = Get-ScheduledTask -TaskName $Name -ErrorAction SilentlyContinue
        if (-not $task) {
            Write-Host "Scheduled task '$Name' is not installed."
            return
        }
        $info = Get-ScheduledTaskInfo -TaskName $Name
        [pscustomobject]@{
            Name = $task.TaskName
            State = $task.State
            LastRunTime = $info.LastRunTime
            LastTaskResult = $info.LastTaskResult
            NextRunTime = $info.NextRunTime
            Description = $task.Description
        } | Format-List
    }
}
