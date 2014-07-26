<!DOCTYPE HTML>
<html>
<head>
<style>
.error {color: #FF0000;}
</style>
</head>
<body>
<@lily
# This is a simple form example in Lily. Code starts at <@lily and ends with @>
# Anything not within those tags is sent as html.
# This form is a modified version of the one here:
# http://www.w3schools.com/php/showphp.asp?filename=demo_form_validation_escapechar

# First, declare the strings that will get used.
str nameError = "", genderError = "", websiteError = "",
    name = "", gender = "", website = "", comment = ""

# server is a package that contains POST and GET vars as server::post and
# server::get respectively. Some environment variables are also available
# through server::env.

# If the request is a POST, then obtain POST values...
if server::httpmethod == "POST": {
    # server::post is type hash[str, str], so it's possible to obtain values
    # from it.
    # However, a key that doesn't exist results in an exception being raised.
    # 'get' is a function that takes a key name, and a default. So if the key
    # isn't found, then "" gets returned.

    # The result of that is then passed to htmlencode, which will safely encode any
    # html entities (&, <, and >)
    name = server::post.get("name", "").htmlencode()

    # Since there's no { after the :, this is a single-line block.
    if name == "":
        nameError = "A name is required."

    gender = server::post.get("gender", "").htmlencode()
    if gender == "":
        genderError = "A gender is required."

    website = server::post.get("website", "").htmlencode()
    comment = server::post.get("comment", "").htmlencode()
else:
    # This else is useless, except to show off Lily's multi-line blocks. The
    # 'if' block is multi-line because of the '{' after the ':'.
    # The block closes when the one '}' is reached: No intermediate {} is
    # needed.
    name = gender = ""
    website = comment = ""
}

@>

<p><span class="error">* required field.</span></p>
<form method="post" action="<@lily print (server::env["SCRIPT_NAME"]) @>">
   Name: <input type="text" name="name" value="<@lily print (name) @>">
   <span class="error">* <@lily print(nameError) @></span>
   <br><br>
   Website: <input type="text" name="website" value="<@lily print (website) @>">
   <span class="error"><@lily print(websiteError)@></span>
   <br><br>
   Comment: <textarea name="comment" rows="5" cols="40"><@lily print (comment) @></textarea>
   <br><br>
   Gender:
   <input type="radio" name="gender" <@lily if gender == "female": print ("checked") @> value="female">Female
   <input type="radio" name="gender" <@lily if gender == "male": print ("checked") @> value="male">Male
   <span class="error">* <@lily print (genderError) @></span>
   <br><br>
   <input type="submit" name="submit" value="Submit">
</form>

<@lily

printfmt("<h2>Your Input:</h2> name: %s <br> website: %s <br> comment: %s <br> gender: %s ",
     	name, website, comment, gender)
@>
