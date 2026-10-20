# Goal
i would like for you to build an IDE/ai chat tool that mostly mimics visual studio(not vs code) but with AI features for local ai.

# what is this 
IED is an IDE/ai chat tool that follows in the explosion theming of my projects like C4(the language) and nuke (the build system).

## general structure
this application will use the C4 programming language as a backend with web based html/css/js as the frontend

it will follow a plugin architecture to help make the application extendable. we will support both global plugins as well as project local plugins if you have that project listed as a trusted workspace. the local plugins will be needed so the C4 language can configure its own vendored language plugin and LSP as well as something like a game engine having a plugin for GPU debugger or LSP for a custom shader language etc.

the main parts of the project will be implemented as plugins.

there will be a thin side bar with icons that plugins can(or might not) add a new tab to like in vs code

plugins can also register skills and tool calls or hook into binding that are provided by other plugins to compose plugins on plugins

we will have application wide settings and project settings. we will have to put the project settings and the skills in a hidden folder in the root of the solution so if we delete the generated files the settings and skills will not be lost.

use a dark theme. no off gray coloring everything should be gray scale unless something is delicately colored to stand out or cary some meaning.

we will need to update the C4 compiler/LSP/DAP/buildSystem(nuke) and the C4 editor plugin (re write as it is currently for another editor and made in go) to work with this new IDE rather then with the old IDE.

this IDE is being built primarily(but not exclusively) for the C4 language and my game engine so nothing should be explicit support for the language or engine but generic features that can be used by the language and engine to get high quality support through plugins and LSP/DAP etc.

C4 -> C4/ (in this repository)
nuke -> Nuke/ (in this repository)

we will need a proper settings window to configure everything. this will be an icon at the bottom of the side bar.

## AI chat tools

we will need chat tools that can be used by the plugins to interact with the ai.
in the settings we can configure the AI models and configure the model groups so you can have different models for things like research sub agents or worker sub agents.

the group we should be able to configure different models for planing research worker etc with different temperatures so we can for example have something like claude do the planing then have a temp zero local model do the implementations. the main ai should be able to delegate work to other AI models that way the smart models can do the planing then hand off implementation work to the smaller local models to help save on tokens usage.

being able to rout request around when you hit token limits or to pick the best model for a given task taking into account what models are busy and if a request needs more privacy so personal information does not get out to cloud models. 

we will want to also configure connections for the ai to use things like SearXNG(have it packaged with this application if possible so we dont need to configure it separately) VPN/tor tools for web searches(searXNG will have to route through this to prevent rate limiting and general privacy) and fetches would also be nice to prevent getting rate limited(we can enable secure mode for the project that will use tor or a configured VPN when accessing the internet)

we will need fast and high quality context compression so we can have long running chats

the chat should be able to read an AGENTS.md file as part of the system message and the plugins should also be able to add a small section to the system message to tell the ai extra information like "we have some docs for this over here <some file path>"

MCP support for things like blender or interacting with the game engine/other projects i use this for

the AI should be able to search for skills based on keywords that way it can still find skills even if it has a massive amount of them.

attaching files and using @ to get file path suggestions.

a strong system prompt to help the ai design and write high quality code with my preferred styling

this is primarily for local AI but integration with Claude using the Claude Agent SDK would be nice though i would like to have views for the usage of my subscription so i know how much i have left in the sessions + rollover times etc like is currently in claude code in the chat window.

save chats to disk in case of crash and keep and evict old chat messages from memory.

any tool calls that you can think of for writing code and working with games/game engines.

i have had some issues withe local models stopping randomly. i dont know if that was the model its self or the harness but we should have some protections for that.

## projects
projects will have a system similar to visual studio where each project has a file to configure it and a solution file that will group them with extra configuration like project local plugins the main difference is that each project file can define a root folder other then the one it is in that way we can generate the project files and put them in a folder in a build directory rather then filling up all the folders.

the project will have a skills folder in the root of the solution(this can be outside of the folder where we might put generated project files that way if we delete the generated file the skills are still there)

in the side bar at the top we should be able to switch between open projects and open federated projects as well as make new ones.

although this is built for projects it would be nice to be able to open single files in it in the right click menu in the file browser or open a folder in the file browser with the same right click menu or even just type "ied ./" to open that folder in ied without making it a project just like in VS code.

## default plugins

### chat
we will need a chat plugin that will be the first plugin at the top. this plugin will have multiple browser like tabs for the chats where you can chat with the ai to get it to do work you will be able to have multiple chats open with tabs at the top you can see the thinking and tool calls in real time as they are being typed out and get basic information like did a tool call finish successful or did if fail and how long did it take. how long did each thinking or normal message take and token counts as well as token speeds down by the chat bar. you will also be able to select a model configuration as well as set the effort as well as all the other features you will need from an ai. you should be able to dock the tabs so you can see multiple chats at the same time but there are no free floating chat windows.

this chat plugin will also define tool calls for things like shell access file system access searches read and write files. load images/video(if supported by the ai). take screen shots or render web pages / load them to get the console to do web work as well as interact with the web page and many more default tool calls.

it would be nice to have a collapsible breakdown on the right side showing the context usage and a list of modified files and other such information for the chat session.

the ability to inject commands into the chat to help guide it or give it extra work

internal scratch pad for planing and note takeing that can be opened up and edited next to the chat ether as part of the chat window or as a tab that is docked next to the chat window.

milestone/task tracking in a chat window.

separate generic chat not attached to a project.

### the text editor / IDE
under the icon in the side bar we will want a text editor that will use the vs code monaco tool to display text with full LSP support through LSP plugins. we want this to act as much like normal visual studio/VSX as possible some features like code formatting will need to be implement in a language plugin or LSP(you can look at the LSP in C4/) we will also want a robust code aware spell check.

on the left side we will have the project/folder structure where projects are given groups and we can see the files in each project just like in visual studio with nice icons to easily tell everything apart at a glance.

we will also need to have the tool bar at the top where you can configure per project dropdowns and buttons with the ability to inject the values for the drop downs into the commands for the buttons and configure images for the buttons that way we can configure dropdowns for platform,configuration, architecture, or any other configuration and have them be used by configured run/test/build/debug etc. the buttons will execute a list of command with different outputs configured so a run button can be configured to build and output to the build output then if the build was successful then run the application and output that to the debug or run output. all the buttons and drop downs are configurable though the DAP support its self is built in.

this will also expose skills and tool calls to the AI so it can build run debug and access the debugger/LSP and other custom LSP endpoints like formatting etc to help the AI navigate the code. giving the AI access to the LSP or other tool calls that wrap the LSP will allow for the AI to navigate code easily without needing to read the whole files.

the text editor will need search features both in the file but search project wide or solution wide with regex or no regex and replace etc like in visual studio

we will also need an output window to see the results of builds or tests etc. the different buttons can set to output to different tabs in the output window(the tabs are a drop down not actual tabs again like in visual studio) this way we can have a build output and run/debug output a test output. and the output window should cary the colors that are given to a normal console.

we should support split views and even bound split views so we can auto open header/source files that go together.

we should have markdown preview support with interactive elements(useful for checking off things in a table among other things) and built in browser to test web pages and support to load up any generic file.

tools to fix indentation in files(i prefer tabs over spaces so we should default to tabs the ai should also be told to always use tabs as part of the system prompt).

inline AI chat tools to edit files in place.

code minimap on the scroll bar.

showing what lines have been changed and saved(Green) and what changes have not been saved to disk(yellow like in visual studio) and other color indicators that are useful.

visual studio/VSX style right click menu and other tools such as LSP navigation high quality code completion(this might need to be part of the LSP) keyboard short cuts and any other tools.

again when i say visual studio im taking about visual studio proper NOT vs code

show indentation by default.

### git
we should have a plugin for git that will show up under the text editor this is a git tool like fork or git gitkraken.

### terminal
we should have a terminal that can run powershell/bash commands. this is not a tab in the side bar but a always present collapsible shell at the bottom of the window takeing up the full width(except for the side bar)

### linux sandbox
this is a sandbox that uses docker to run some container to build, test and develop a project on another operating system. this is something that the AI should be able to use to autonomously test that something works on linux or other operating systems that can be run in docker like macOS

### profiler
we should have a tracy like profiler that is fully featured to find and resolve performance issues and memory leaks. we should also allow for threads to change there name in the middle of execution(with a background tint to show the name changed) this way if we have a pool of worker threads and they switch tasks we can view them by task rather then by thread in the view. we will also need to support GPU trace information.

we should build out the C4 default library to be able to use this profiler by default.

## process
commit along the way at checkpoints
make sure to properly test all interactions and validate the UI/UX looks nice and the UX is an nice and clear as possible. take plenty of screen shots to validate that everything works including the user interactions.

design/plan -> implement -> test -> review -> repeat.