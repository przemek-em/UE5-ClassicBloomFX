cd D:\UE\UnrealEngine-5.6.1-release
Remove-Item -Recurse -Force "Engine\Plugins\Experimental\ClassicBloomFX\Binaries"
Remove-Item -Recurse -Force "Engine\Plugins\Experimental\ClassicBloomFX\Intermediate"
.\Engine\Build\BatchFiles\Build.bat UnrealEditor Win64 Development -Plugin="D:\UE\UnrealEngine-5.6.1-release\Engine\Plugins\Experimental\ClassicBloomFX\ClassicBloomFX.uplugin"