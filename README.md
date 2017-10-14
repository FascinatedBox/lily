Linux: [![Linux Build](https://travis-ci.org/FascinatedBox/lily.svg?branch=master)](https://travis-ci.org/FascinatedBox/lily)

Windows: [![Windows Build](https://ci.appveyor.com/api/projects/status/github/FascinatedBox/lily?svg=true)](https://ci.appveyor.com/project/FascinatedBox/lily)

## Lily

Lily is a programming language focused on expressiveness and type safety.

## Sample

```
scoped enum Color { Black, Blue, Cyan, Green, Magenta, Red, White, Yellow }

class Terminal(var @foreground: Color, width_str: String)
{
    public var @width = width_str.parse_i().unwrap_or(80)

    public define set_fg(new_color: Color) {
        @foreground = new_color
    }
}

var terms = [Terminal(Color.White, "A"), Terminal(Color.Red, "40")]

terms.each(|e| e.width += 20 )
     |> print
```

## Features

#### Templating

By default, Lily runs in **standalone** mode where all content is code to
execute. But Lily can also be run in **template** mode. In **template** mode,
code is between `<?lily ... ?>` tags. When a file is imported, it's always
loaded in **standalone** mode, so that it doesn't accidentally send headers.
Files that are imported are also namespaced (no 'global namespace').

#### Embeddable

Lily may be a statically-typed language, but the reference implementation is an
interpreter. The interpreter as well as its API have been carefully designed
with sandboxing in mind. As a result, it's possible to have multiple
interpreters exist alongside each other.

#### Shorter edit cycle

Another benefit from having the reference implementation as an interpreter is a
shorter turn around time. The interpreter's parser is comparable in speed to
that of languages using an interpreter as their reference.

## Community

Lily is a very young language and the community is still growing.

* Discord: [Lily](https://discord.gg/Vr5CXFY) to chat with others in real-time.

* IRC: [freenode #lily](https://webchat.freenode.net/?channels=%23lily) is
  another way to chat with others.

* Reddit: [/r/lily](https://reddit.com/r/lily) for discussion around the
  language and providing support to new users.

## Resources

* [Reference](https://Fascinatedbox.github.com/lily/core/module.core.html)

* [Try it in your browser](https://FascinatedBox.github.com/lily-site/sandbox.html)
