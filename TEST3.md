## A Brief Introduction to ChatGTK
While every effort has been made to make the application intuitive to use, it is a fairly complex beast due to the number of features and range of large language models supported. Most buttons have tooltips and many features are entirely optional and can be safely ignored if desired. Below is a brief overview of some of the main capabilities of the application for the folk that still read documentation.

[API Keys](#1.-Adding-API-Keys)
[Tools](#2.-Using-tools)
[System Prompts](#3.-Adding-a-custom-system-prompt)
[Markdown](#4-Using-markdown)
[Projects](#5-Projects)
[Images](#6.-Generating-and-editing-images)
[Audio](#7.-Using-Speech-to-text-and-text-to-speech)

---
### 1. Adding API Keys
API keys are a code that allows you to access AI models on a given providers servers. This application is next to useless without one or more keys.

For the application to function it needs to know about your API keys. There are two main way to accomplish this - **environment variables** and **directly** entering them into the applicatons settings dialog.

To **directly** enter a key you can simply copy and paste it into the relevent text box in `settings`-->`API Keys`. Clicking save will persist your keys to the applications settings and you woll not need to enter them again.

**Environment variables** are little more complicated and somewhat dependent on which distrobution of Linux and window manager you are using. Therefore I will defer all discussion on this topic to you favourite AI assistant.
---
### 2. Using tools
Tools are a way to expand the capabilities of an AI model. They work by giving the AI a utitlity that they can choose to run if they deem it appropriate. This is how features such as image generation and websearch work in the commercial chat apps. The model does not actually look up things on google for you, it instead calls a function using a short code, something like: `websearch: "cat pics", "5 results"`. The AI will then get the results from that function and use them to inform its reply.

Tools are enabled and disabled in `Settings`-->`Tool Options`. When enabled they become available in the `tools` menu in the menu bar.

- **Image Tool**:This allow the models to generate a pictures using the image model you have specified in `settings`-->`Tools`.
- **Music Tool**:Allows for the model to generate playlists for you from your own locally stored music as well as starting and stopping playback.
- **Read Aloud Tool**: This lets a model speak using the builtin text-to-speech system when prompted to do so. You can ask "Write a Haiku for me and read it aloud!".
- **Search Tool**: This gives models the ability to search a folder on your local disk for text keywords. It can do this to retrieve relevent information to a user query. For example if the history folder that contains previous conversations you have had with the model is shared it allows the model to find relevent snippits to your current query.
- **Text Edit Tool**: Enables a model to read or edit a file on your behalf. When enabled the paperclip icon will set the target file and you ask a model to "add a paragraph about bats to my document" or "please correct the spelling in my file".
- **Web Search Tool**: Currently this enables server side tools for search capabitlities that is offered by some models. Such models include xAI's **Grok 4** series, OpenAI's more recent models, Googles **Gemini** models and of course the Perplexity models. Not all models support this feature yet.
- **Wolfram Alpha Tool**: Allows the model to query [wolframalpha](https://www.wolframalpha.com/). Touse this tool you need to register at the [developer portal](https://developer.wolframalpha.com/) in order to get a an `app_id`. You enter this code in the provided area in `settings`-->`API Keys`.

Each tool has an associated piece of text that explains to the LLM how and when to operate that tool. You can find and if desired edit these in `Settings`-->`Advanced`.

---
### 3. Adding a custom system prompt
The system prompt is the most important way you can control how a model behaves. You can modify its character, which language it prefers, change how it formats its replies and a litany of other behavioural changes.

In `Settings`-->`System Prompt` you find an area where you can add as many system prompts as you desire along with some default ones. You can then set the system prompt using the dropdown list in toolbar above. You can switch system prompts at any time, even mid conversation"
---
### 4. Using markdown
**Markdown** is a lightweight formatting syntax used to style messages and documentation by humans and AI alike. In this application markdown can be used both in the document editor (where you are now) and in the prompt editor, which you can open by clicking on the pencil icon to right of the question box at the bottom of the application. Pressing the preview button will switch between the rendered markdown and the raw text with the symbols used to define how the text is to be formatted.

- For headings of different sizes use `#`, `##`, `###`.
- Use `-` or `1.` for bulleted or numbered lists.
- Use blank lines to separate paragraphs.
- Use `**bold**` and `*italic*` for emphasis.
- Three dashes `---` will create a dividing line.
- Enclose a `word` in single backticks for inline code and use triple backticks for code blocks or becouse you just like to put things in a box.
```python
x = 1
if x == 1:
    # indented four spaces
    print("x is 1.")
```
To learn more about markdown you can experiment in a document or in the prompt editor dialog. Your AI will be able to fill you in on the rest of the details.
---
### 5. Projects
The projects icon is at the very left of the upper toolbar. Creating a new project will give you a fresh empty sidebar where you can work on a specific task without extraneous clutter. 
The current open project is always present in the applications titlebar. By default you are in the home project if you have not created another, otherwise the application will open in the last used project.

You can move files between projects by right clicking on them in the history sidebar and selecting `move to project`. If you have that conversation or document open while doing this it will also move you to the project as well! This can also serve as a shortcut to creating a new project as the option to do so exists in the resulting dialog.

`Manage Projects` in the projects menu allows for the deletion and renaming of projects.
---
### 6. Generating and editing images
There are three ways images can be generated or edited in the application.

- **Direct calls** to image editing models.
- **Tool calls** to image generation models by any model capable of function calling.
- **Multimodel models** capable of generating images server-side.

To make a **direct call** silmply select an image model from the drop down model list and send your prompt. The image will appear in the conversation area. Some examples of some pure image generation models are `gpt-image-1`, `flux.2-pro`, `nano-bananna` and `seedream`. The models available to you will depend on which are provided by you API key provider.

To use a **tool call** you first enable the image tool in the `Tools` menu at the top right of the main UI window. Now any model you are conversing with will have the ability to use an image generation model on your behalf. The model it uses for the generation is the one configured in *`settings`⇒`tool options`⇒`Image Tool Model`*. To trigger this behaviour simply indicate to the model it should use the tool, for example ask "Make me an image of a cat in style of H.R. Giger".😈️

---
### 7. Using speech-to-text and text-to-speech
- Pressing the playback button in the top right of a message from the assistant will send the text in that message to the text-to-speech model select in `Settings`-->`Audio`. Playback will begin automatically once the audio is recieved from the server.
- Audio is cached so pressing playback a second time wil be instantaneous.
- If you change the text-to-speech model or the voice settings the speech will regenerate next time you press the playback button.
---
### 8. Adding a custom model
---
### 9. Editing Documents
---
### Troubleshooting
If you launch the application from a terminal will get some logging messages which may help you pin down why something is not working. If you encounter a bug feel free to report it at the github issues tracker for ChatGTK: [Github](https://github.com/rabfulton/ChatGTK/issues)

---
If you made this far, feel free to delete this document. Right click on `Tutorial` in the sidebar to your left, and select `Delete Document`.

If you hated this tutorial feel free to write a better one and submitt a pull request on the github page, and recall that no warranty was expressed or given.